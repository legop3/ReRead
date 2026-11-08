// One camera owner, with bounded capture/processing queues. The hardware
// worker never waits for browser traffic or JPEG/image processing.
#include "camera_native.hpp"
#include "camera_hardware.hpp"
#include "camera_processing.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <sched.h>
#include <stdexcept>
#include <thread>
#include <vector>

namespace reread {
namespace {
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
struct PhotoJob {
    bool flash = false, document = false, done = false, rotated = false;
    std::string path, preview_path, error;
    std::vector<uint16_t> raw;
    ReaderCameraPhoto result{};
};
void save(const std::string& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cannot save camera image: " + path);
}
class Engine {
    std::mutex mutex_;
    std::condition_variable wake_, frames_, complete_;
    std::thread hardware_, processing_;
    bool stopping_ = false, running_ = false, hardware_done_ = false;
    Time lease_{}, jpeg_lease_{};
    std::shared_ptr<PhotoJob> requested_, photo_, processing_photo_;
    std::deque<std::vector<uint16_t>> queue_;
    std::deque<Time> capture_times_, display_times_;
    std::vector<unsigned char> latest_, rgb_;
    uint64_t jpeg_sequence_ = 0;
    uint64_t sequence_ = 0, received_ = 0, dropped_ = 0, damaged_ = 0, delivered_ = 0;
    std::string error_;
    int width_ = 0, height_ = 0;

    void enqueue(std::vector<uint16_t>& frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++received_;
        capture_times_.push_back(Clock::now());
        if (capture_times_.size() > 32) capture_times_.pop_front();
        if (queue_.size() == 2) { queue_.pop_front(); ++dropped_; }
        queue_.emplace_back();
        queue_.back().swap(frame);
        wake_.notify_all();
    }
    void fail_job(const std::shared_ptr<PhotoJob>& job, const std::string& error) {
        if (!job) return;
        job->error = error;
        job->done = true;
        complete_.notify_all();
    }
    void hardware_loop() {
        // Matching stock's stream scheduler; only this USB worker is FIFO.
        sched_param priority{};
        priority.sched_priority = sched_get_priority_min(SCHED_FIFO) +
            (sched_get_priority_max(SCHED_FIFO)-sched_get_priority_min(SCHED_FIFO))*20/100;
        if (sched_setscheduler(0, SCHED_FIFO, &priority) < 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            error_ = "Cannot set camera capture scheduling priority";
            stopping_ = true; hardware_done_ = true;
            frames_.notify_all(); wake_.notify_all(); return;
        }
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&]{return stopping_ || requested_ || Clock::now() < lease_;});
                if (stopping_) break;
            }
            try {
                Camera camera;
                camera.start_stream();
                camera.begin_async_stream();
                const auto opened = Clock::now();
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    running_ = true; error_.clear();
                    received_ = delivered_ = dropped_ = damaged_ = 0;
                    capture_times_.clear(); display_times_.clear();
                }
                // Persistent framing crosses USB reads. A transfer is not
                // assumed to start at a frame or line boundary.
                constexpr size_t width = 648, height = 480, pixels = width*height;
                std::vector<uint16_t> burst, frame;
                frame.reserve(pixels);
                bool valid = false;
                for (;;) {
                    std::shared_ptr<PhotoJob> job;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (stopping_ || (!requested_ && Clock::now() >= lease_)) break;
                        // A fresh device streams while exposure/focus settles,
                        // as it does while the user frames a stock photograph.
                        if (requested_ && Clock::now()-opened >= std::chrono::seconds(2)) {
                            job = requested_; requested_.reset();
                        }
                    }
                    if (job) {
                        try {
                            camera.end_async_stream();
                            camera.capture_still(job->raw, job->flash, true);
                            const auto focus = camera.last_focus;
                            job->result.raw = focus.raw; job->result.status = focus.status;
                            job->result.gain = focus.gain; job->result.prelocked = focus.prelocked;
                            std::lock_guard<std::mutex> lock(mutex_);
                            queue_.clear(); processing_photo_ = job; wake_.notify_all();
                        } catch (const std::exception& error) {
                            std::lock_guard<std::mutex> lock(mutex_);
                            fail_job(job, error.what());
                        }
                        camera.begin_async_stream();
                        frame.clear(); valid = false;
                        continue;
                    }
                    camera.read_async_stream(burst);
                    for (uint16_t word : burst) {
                        if (word & 0x8000) {
                            if (valid && !frame.empty()) {
                                std::lock_guard<std::mutex> lock(mutex_); ++damaged_;
                            }
                            frame.clear(); valid = true;
                        }
                        if (!valid) continue;
                        if (bool(word & 0x1000) != (frame.size()%width == 0)) {
                            frame.clear(); valid = false;
                            std::lock_guard<std::mutex> lock(mutex_); ++damaged_;
                            continue;
                        }
                        frame.push_back(word);
                        if (frame.size() == pixels) {
                            enqueue(frame); frame.reserve(pixels); valid = false;
                        }
                    }
                }
                camera.stop_stream();
            } catch (const std::exception& error) {
                std::lock_guard<std::mutex> lock(mutex_);
                error_ = error.what(); lease_ = Time{};
                fail_job(requested_, error_); requested_.reset();
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                running_ = false; frames_.notify_all();
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hardware_done_ = true; wake_.notify_all();
        }
    }
    void processing_loop() {
        for (;;) {
            std::shared_ptr<PhotoJob> job;
            std::vector<uint16_t> raw;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&]{return (stopping_ && hardware_done_) || processing_photo_ || !queue_.empty();});
                if (processing_photo_) { job = processing_photo_; processing_photo_.reset(); }
                else if (stopping_ && hardware_done_) break;
                else {
                    raw.swap(queue_.back());
                    dropped_ += queue_.size()-1; queue_.clear();
                }
            }
            if (job) {
                try {
                    Image image = process_frame(job->raw, 2592, 1920, true, job->document);
                    job->raw.clear(); job->raw.shrink_to_fit();
                    save(job->path, jpeg(image, 90, 0, true, true, job->rotated));
                    save(job->preview_path, jpeg(image, 85, 960, true, true, job->rotated));
                    std::lock_guard<std::mutex> lock(mutex_);
                    job->result.width = image.width; job->result.height = image.height;
                    job->done = true; complete_.notify_all();
                } catch (const std::exception& error) {
                    std::lock_guard<std::mutex> lock(mutex_); fail_job(job, error.what());
                }
            } else {
                try {
                    Image image = process_frame(raw, 648, 480, false);
                    bool encode;
                    { std::lock_guard<std::mutex> lock(mutex_); encode = Clock::now() < jpeg_lease_; }
                    auto compressed = encode ? jpeg(image, 75) : std::vector<unsigned char>();
                    std::lock_guard<std::mutex> lock(mutex_);
                    rgb_.swap(image.rgb); ++sequence_; ++delivered_;
                    if(encode){latest_.swap(compressed); jpeg_sequence_ = sequence_;}
                    display_times_.push_back(Clock::now());
                    if (display_times_.size() > 32) display_times_.pop_front();
                    width_ = image.width; height_ = image.height; frames_.notify_all();
                } catch (const std::exception& error) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++damaged_; error_ = error.what(); frames_.notify_all();
                }
            }
        }
    }
public:
    Engine() {
        // Probe synchronously so service status does not promise an absent
        // camera. Firmware loading is performed by the control layer first.
        { Camera probe; probe.stop_stream(); }
        hardware_ = std::thread(&Engine::hardware_loop, this);
        try { processing_ = std::thread(&Engine::processing_loop, this); }
        catch (...) { shutdown(); throw; }
    }
    ~Engine() { shutdown(); }
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            fail_job(requested_, "Camera is stopping"); requested_.reset();
            wake_.notify_all(); frames_.notify_all(); complete_.notify_all();
        }
        if (hardware_.joinable()) hardware_.join();
        // A hardware capture that was already underway may have queued its
        // image before exiting. Finish it before releasing its waiter.
        wake_.notify_all();
        if (processing_.joinable()) processing_.join();
    }
    std::vector<unsigned char> frame(uint64_t after, uint64_t& sequence, bool rgb = false, int* width = nullptr, int* height = nullptr) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopping_) throw std::runtime_error("Camera is stopping");
        lease_ = Clock::now() + std::chrono::seconds(8);
        if(!rgb) jpeg_lease_ = lease_;
        wake_.notify_all();
        frames_.wait_for(lock, std::chrono::seconds(10), [&]{
            return stopping_ || (rgb ? (!rgb_.empty() && sequence_ != after) : (!latest_.empty() && jpeg_sequence_ != after)) || !error_.empty();
        });
        if (stopping_) throw std::runtime_error("Camera is stopping");
        if (rgb ? rgb_.empty() : latest_.empty()) throw std::runtime_error(error_.empty()?"Camera preview is starting":error_);
        if(!error_.empty()) throw std::runtime_error(error_);
        if((rgb ? sequence_ : jpeg_sequence_) == after)
            throw std::runtime_error("No new camera preview frame");
        sequence = rgb ? sequence_ : jpeg_sequence_;
        if(width) *width = width_;
        if(height) *height = height_;
        return rgb ? rgb_ : latest_;
    }
    ReaderCameraPhoto capture(bool flash, bool document, int rotation, const char* path, const char* preview) {
        auto job = std::make_shared<PhotoJob>();
        job->flash = flash; job->document = document; job->rotated = rotation == 180; job->path = path; job->preview_path = preview;
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopping_) throw std::runtime_error("Camera is stopping");
        if (photo_ && !photo_->done) throw std::runtime_error("Camera is already taking a photo");
        photo_ = job; requested_ = job; error_.clear(); wake_.notify_all();
        complete_.wait(lock, [&]{return job->done || stopping_;});
        if (!job->done) throw std::runtime_error("Camera stopped before the photo completed");
        if (!job->error.empty()) throw std::runtime_error(job->error);
        return job->result;
    }
    ReaderCameraStats stats() {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = Clock::now();
        auto rate = [&](std::deque<Time>& times) {
            while (!times.empty() && now-times.front() > std::chrono::seconds(2)) times.pop_front();
            if (!running_ || times.size() < 2) return 0.0;
            return (times.size()-1)/std::chrono::duration<double>(times.back()-times.front()).count();
        };
        return {received_, delivered_, dropped_, damaged_, rate(capture_times_), rate(display_times_),
                running_?1:0, width_, height_};
    }
};
void error_message(char* output, size_t size, const std::exception& error) {
    if (output && size) std::snprintf(output, size, "%s", error.what());
}
}
}
extern "C" {
void* reader_camera_create(char* error, size_t size) {
    try {return new reread::Engine();}
    catch (const std::exception& exception) {reread::error_message(error,size,exception);return nullptr;}
}
void reader_camera_shutdown(void* engine) { if (engine) static_cast<reread::Engine*>(engine)->shutdown(); }
void reader_camera_destroy(void* engine) { delete static_cast<reread::Engine*>(engine); }
void reader_camera_free(void* data) {std::free(data);}
int reader_camera_frame(void* engine,uint64_t after,unsigned char** output,size_t* size,
                        uint64_t* sequence,char* error,size_t error_size) {
    try {
        auto data=static_cast<reread::Engine*>(engine)->frame(after,*sequence);
        *output=static_cast<unsigned char*>(std::malloc(data.size()));
        if (!*output) throw std::bad_alloc();
        std::memcpy(*output,data.data(),data.size());*size=data.size();return 0;
    } catch (const std::exception& exception) {reread::error_message(error,error_size,exception);return -1;}
}
int reader_camera_rgb_frame(void* engine,uint64_t after,unsigned char** output,size_t* size,
    uint64_t* sequence,int* width,int* height,char* error,size_t error_size) {
    try {
        auto data=static_cast<reread::Engine*>(engine)->frame(after,*sequence,true,width,height);
        *output=static_cast<unsigned char*>(std::malloc(data.size()));
        if(!*output) throw std::bad_alloc();
        std::memcpy(*output,data.data(),data.size());*size=data.size();return 0;
    } catch(const std::exception& exception){reread::error_message(error,error_size,exception);return -1;}
}
int reader_camera_capture(void* engine,int flash,int document,int rotation,const char* path,const char* preview,
                          ReaderCameraPhoto* photo,char* error,size_t error_size) {
    try {*photo=static_cast<reread::Engine*>(engine)->capture(flash,document,rotation,path,preview);return 0;}
    catch(const std::exception& exception){reread::error_message(error,error_size,exception);return -1;}
}
void reader_camera_stats(void* engine,ReaderCameraStats* stats) {
    *stats=static_cast<reread::Engine*>(engine)->stats();
}
}
