// Recovered from Intel's libcolbert-camera.so (SHA256 b8cf21e44aad76ea...
// 596b5262b6b). See analysis/camera-protocol.md. This implementation sends
// volatile camera commands only; it never writes EEPROM or FPGA firmware.
#include "camera_hardware.hpp"
#include <libusb-1.0/libusb.h>
#include <algorithm>
#include <array>
#include <deque>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sched.h>
#include <stdexcept>
#include <thread>

namespace reread {
namespace {
using Clock = std::chrono::steady_clock;
void pause_ms(unsigned milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
void check(int result, const char* operation) {
    if (result < 0)
        throw std::runtime_error(std::string(operation) + ": " + libusb_error_name(result));
}
// Stock ScopedSchedulerChange uses FIFO20 for stream, FIFO100 for still.
// Scope is the calling capture thread, never the web/control thread.
class TransferPriority {
    int policy_;
    sched_param parameters_{};
    bool changed_ = false;
public:
    explicit TransferPriority(int percent) : policy_(sched_getscheduler(0)) {
        if (policy_ < 0 || sched_getparam(0, &parameters_) < 0)
            throw std::runtime_error("Cannot read capture scheduling policy");
        sched_param current{};
        int lo = sched_get_priority_min(SCHED_FIFO);
        current.sched_priority = lo + (sched_get_priority_max(SCHED_FIFO) - lo) * percent / 100;
        if (sched_setscheduler(0, SCHED_FIFO, &current) < 0)
            throw std::runtime_error("Cannot set camera transfer priority");
        changed_ = true;
    }
    ~TransferPriority() { if (changed_) sched_setscheduler(0, policy_, &parameters_); }
};
uint16_t chassis_focus_calibration() {
    std::ifstream stream("/sys/firmware/dmi/tables/DMI", std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read SMBIOS camera focus calibration");
    std::vector<uint8_t> table((std::istreambuf_iterator<char>(stream)), {});
    for (size_t offset = 0; offset + 4 <= table.size();) {
        unsigned type = table[offset], length = table[offset + 1];
        if (length < 4 || offset + length > table.size()) break;
        if (type == 3 && length >= 17)
            return table[offset + 13] | uint16_t(table[offset + 14]) << 8;
        size_t end = offset + length;
        while (end + 1 < table.size() && (table[end] || table[end + 1])) ++end;
        if (end + 1 >= table.size()) break;
        offset = end + 2;
    }
    throw std::runtime_error("SMBIOS chassis focus calibration missing");
}
}
// Four outstanding transfers prevent userspace processing/scheduling gaps from
// leaving the small FX2 image FIFO without an active host read. All callbacks
// run on the same owner thread inside libusb_handle_events, never concurrently
// with register commands. Frame boundaries may cross transfer boundaries.
struct Camera::AsyncStream {
    static constexpr size_t words_per_transfer = 648 * 480;
    struct Slot {
        AsyncStream* owner = nullptr;
        libusb_transfer* transfer = nullptr;
        std::vector<uint16_t> words;
        bool pending = false;
    };
    std::array<Slot, 4> slots;
    std::deque<std::vector<uint16_t>> ready;
    std::string error;
    bool stopping = false;
    unsigned pending = 0;
    static void LIBUSB_CALL completed(libusb_transfer* transfer) noexcept {
        auto& slot = *static_cast<Slot*>(transfer->user_data);
        auto& state = *slot.owner;
        slot.pending = false;
        --state.pending;
        if (state.stopping) return;
        try {
            if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
                state.error = "Asynchronous camera transfer failed: " +
                              std::to_string(int(transfer->status));
                return;
            }
            if (transfer->actual_length % 2) {
                state.error = "Asynchronous camera transfer ended within a pixel";
                return;
            }
            if (transfer->actual_length > 0) {
                if (state.ready.size() == 4) state.ready.pop_front();
                state.ready.emplace_back(slot.words.begin(),
                                         slot.words.begin() + transfer->actual_length / 2);
            }
            int result = libusb_submit_transfer(transfer);
            if (result < 0) {
                state.error = std::string("Resubmit camera stream: ") + libusb_error_name(result);
                return;
            }
            slot.pending = true;
            ++state.pending;
        } catch (...) {
            // C callbacks must not unwind through libusb.
            try { state.error = "Cannot buffer asynchronous camera frame"; } catch (...) {}
            state.stopping = true;
        }
    }
};
void Camera::begin_async_stream() {
    if (async_) return;
    if (!device_ || !active_) throw std::runtime_error("Camera stream is not running");
    async_ = new AsyncStream;
    try {
        check(libusb_clear_halt(device_, 0x82), "Clear camera stream endpoint");
        // Allocate every transfer before submitting, preventing allocations
        // between the first USB submission and establishing the full queue.
        for (auto& slot : async_->slots) {
            slot.owner = async_;
            slot.words.resize(AsyncStream::words_per_transfer);
            slot.transfer = libusb_alloc_transfer(0);
            if (!slot.transfer) throw std::runtime_error("Cannot allocate USB camera transfer");
            libusb_fill_bulk_transfer(slot.transfer, device_, 0x82,
                    reinterpret_cast<uint8_t*>(slot.words.data()), slot.words.size() * 2,
                    AsyncStream::completed, &slot, 5000);
        }
        TransferPriority priority(20);
        for (auto& slot : async_->slots) {
            check(libusb_submit_transfer(slot.transfer), "Queue camera stream transfer");
            slot.pending = true;
            ++async_->pending;
        }
    } catch (...) { end_async_stream(); throw; }
}
void Camera::read_async_stream(std::vector<uint16_t>& output) {
    if (!async_) throw std::runtime_error("Asynchronous camera stream is not running");
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    TransferPriority priority(20);
    for (;;) {
        if (!async_->error.empty()) throw std::runtime_error(async_->error);
        if (!async_->ready.empty()) {
            output = std::move(async_->ready.front());
            async_->ready.pop_front();
            return;
        }
        if (async_->stopping || !async_->pending)
            throw std::runtime_error("Asynchronous camera stream stopped");
        if (Clock::now() >= deadline) throw std::runtime_error("Timed out waiting for camera stream");
        timeval timeout{0, 20000};
        int result = libusb_handle_events_timeout_completed(context_, &timeout, nullptr);
        if (result != LIBUSB_ERROR_INTERRUPTED) check(result, "Handle camera stream USB events");
    }
}
void Camera::end_async_stream() noexcept {
    if (!async_) return;
    async_->stopping = true;
    for (auto& slot : async_->slots)
        if (slot.pending) libusb_cancel_transfer(slot.transfer);
    // Cancelled transfers remain owned by libusb until their callbacks run;
    // freeing buffers earlier is a use-after-free. USB-transfer timeouts are
    // 5 seconds, and cancellation completes through libusb events even when
    // the camera is unplugged; do not break early and free pending buffers.
    // A broken libusb event backend cannot safely be abandoned in-process.
    // Cancellation callbacks are
    // dispatched by event handling, including device-disconnection completion.
    while (async_->pending) {
        timeval timeout{0, 20000};
        libusb_handle_events_timeout_completed(context_, &timeout, nullptr);
    }
    for (auto& slot : async_->slots)
        if (slot.transfer) libusb_free_transfer(slot.transfer);
    delete async_;
    async_ = nullptr;
}
Camera::Camera() {
    check(libusb_init(&context_), "Initialize libusb");
    try {
        device_ = libusb_open_device_with_vid_pid(context_, 0x04b4, 0x1005);
        if (!device_) throw std::runtime_error("Programmed Reader camera 04b4:1005 not found");
        int configuration = 0;
        check(libusb_get_configuration(device_, &configuration), "Read camera configuration");
        if (configuration != 1) check(libusb_set_configuration(device_, 1), "Configure camera");
        check(libusb_claim_interface(device_, 0), "Claim camera interface");
        claimed_ = true;
        try { version(); }
        catch (...) {
            // Camera-only USB reset, also used after S3 by the Python probe.
            libusb_release_interface(device_, 0);
            claimed_ = false;
            check(libusb_reset_device(device_), "Reset camera USB synchronization");
            pause_ms(500);
            check(libusb_claim_interface(device_, 0), "Reclaim camera interface");
            claimed_ = true;
            version();
        }
        flash(false);
    } catch (...) { close(); throw; }
}
Camera::~Camera() { close(); }
void Camera::close() noexcept {
    end_async_stream();
    if (device_) {
        if (claimed_) {
            // Attempt each cleanup even if another USB operation fails.
            try { flash(false); } catch (...) {}
            if (active_) { try { set_streaming(false); } catch (...) {} }
            try { write_register(0x17, 0); } catch (...) {}
            try { write_register(0x12, 1); } catch (...) {}
            libusb_release_interface(device_, 0);
        }
        libusb_close(device_);
        device_ = nullptr;
        claimed_ = false;
        active_ = false;
    }
    if (context_) { libusb_exit(context_); context_ = nullptr; }
}
void Camera::write_bytes(const uint8_t* bytes, size_t count) {
    if (!device_) throw std::runtime_error("Camera is closed");
    int sent = 0;
    check(libusb_bulk_transfer(device_, 0x01, const_cast<uint8_t*>(bytes),
                              static_cast<int>(count), &sent, 1000), "Write camera command");
    if (static_cast<size_t>(sent) != count) throw std::runtime_error("Short camera command write");
}
void Camera::read_bytes(uint8_t endpoint, uint8_t* bytes, size_t count,
                        int& received, unsigned timeout) {
    if (!device_) throw std::runtime_error("Camera is closed");
    check(libusb_bulk_transfer(device_, endpoint, bytes, static_cast<int>(count),
                              &received, timeout), "Read camera USB endpoint");
}
void Camera::send(uint8_t command, std::initializer_list<uint8_t> payload,
                  uint8_t subcommand, uint8_t address) {
    const size_t body_size = payload.size() + (address ? 1 : 0);
    const size_t nested_size = body_size + (subcommand ? 3 : 0);
    std::vector<uint8_t> packet;
    packet.reserve(nested_size + 2);
    packet.push_back(command);
    packet.push_back(uint8_t(nested_size));
    if (subcommand) {
        packet.push_back(0x10);
        packet.push_back(subcommand);
        packet.push_back(uint8_t(body_size + 2));
    }
    if (address) packet.push_back(address);
    for (uint8_t byte : payload) packet.push_back(byte);
    write_bytes(packet.data(), packet.size());
}
uint16_t Camera::read_register(uint16_t reg) {
    send(0x0d, {uint8_t(reg >> 8), uint8_t(reg)});
    pause_ms(10); // stock SendAndReceive delay
    uint8_t response[2]; int received = 0;
    read_bytes(0x81, response, 2, received, 1000);
    if (received != 2) throw std::runtime_error("Short FPGA register reply");
    return uint16_t(response[0]) << 8 | response[1];
}
void Camera::write_register(uint16_t reg, uint16_t value) {
    send(0x10, {uint8_t(reg >> 8), uint8_t(reg), uint8_t(value >> 8), uint8_t(value)}, 1);
}
uint16_t Camera::read_i2c(uint8_t address, uint8_t reg) {
    send(0x10, {reg}, 4, address);
    return read_register(0xfffe);
}
void Camera::write_i2c(uint8_t address, uint8_t reg, uint16_t value) {
    send(0x10, {reg, uint8_t(value >> 8), uint8_t(value)}, 3, address);
}
void Camera::set_streaming(bool enabled) {
    const uint8_t packet[]{0x0a, uint8_t(enabled)};
    write_bytes(packet, sizeof(packet));
    pause_ms(1000); // stock SetState: no acknowledgement is returned
}
void Camera::flash(bool enabled) {
    send(0x10, {3, uint8_t(enabled)}, 6);
    send(0x10, {uint8_t(enabled ? 4 : 5)}, 6);
}
void Camera::initialize() {
    // sensor::SetDefaults @119e0; detail::Start @12770.
    const uint16_t defaults[][2] = {
        {0x0d,1},{0x10,0x50},{0x11,0x6404},{0x12,0},{0x0d,0},
        {1,0x32},{2,0x0a},{3,1919},{4,2591},{5,0},{7,0x1f82},
        {8,0},{9,0x1a0},{0x0a,0},{0x0b,0},{0x0c,0},
        {0x2b,8},{0x2c,8},{0x2d,8},{0x2e,8},{0x35,12},{0xa0,0}
    };
    for (auto& setting : defaults) write_i2c(0x5d, uint8_t(setting[0]), setting[1]);
    const uint16_t fpga[][2] = {
        {0x102,15},{0x108,15},{0x104,0x140},{0x17,0},
        {0x19,3},{0x1a,3},{0x15,1919},{0x16,2591},
        {0x1b,0},{0x1c,0},{0x13,1919},{0x14,2591},
        {0x106,0x4cc},{0x105,0x4cc},{0x12,2}
    };
    for (auto& setting : fpga) write_register(setting[0], setting[1]);
    uint16_t calibration = chassis_focus_calibration();
    write_register(0x109, calibration);
    std::fprintf(stderr, "Camera unit focus calibration=%04x\n", calibration);
}
void Camera::start_stream(bool reset) {
    end_async_stream();
    active_ = true; // also clean up partially initialized hardware on exceptions
    if (reset) initialize();
    const uint16_t settings[][2] = {{6,8},{0x20,0xc040},{0x22,3},{0x23,3},{0x1e,0x4006}};
    for (auto& s : settings) write_i2c(0x5d, uint8_t(s[0]), s[1]);
    write_i2c(0x6b, 0, 0x0602);
    write_register(0x12, 2);
    write_register(0x17, 2);
    write_register(0x107, 3);
    for (uint8_t value : {1, 0, 0, 1}) send(0x10, {value}, 5);
    set_streaming(true);
}
void Camera::stop_stream() {
    end_async_stream();
    std::exception_ptr failure;
    try { set_streaming(false); } catch (...) { failure = std::current_exception(); }
    try { flash(false); } catch (...) { if (!failure) failure = std::current_exception(); }
    try { write_register(0x17, 0); } catch (...) { if (!failure) failure = std::current_exception(); }
    try { write_register(0x12, 1); } catch (...) { if (!failure) failure = std::current_exception(); }
    active_ = false;
    if (failure) std::rethrow_exception(failure);
}
uint16_t Camera::focus(double timeout_seconds) {
    end_async_stream();
    // Full recovered focus helper @12da0, including one reinitialization and
    // prelock fallback @135a9. FPGA firmware owns the optical search; host
    // status is not itself a measurement of optical sharpness.
    last_focus = {};
    write_register(0x12, 2);
    write_register(0x107, 3);
    set_streaming(false);
    const auto deadline = Clock::now() + std::chrono::milliseconds(int(timeout_seconds * 1000));
    bool reinitialized = false, prelocked = false;
    uint16_t previous_position = 0;
    unsigned repeated_prelocks = 0;
    while (Clock::now() < deadline) {
        pause_ms(250);
        read_register(0x1000);
        uint16_t raw = read_register(0x1000);
        read_register(0x1005);
        uint16_t status = read_register(0x1005);
        // Although 1302f initially puts the first 1005 read in ESI,
        // 13047 -> 12ee8 overwrites ESI from the SECOND read (local c62)
        // on the valid raw!=status path. Both bit14 and low12 position
        // therefore use this second value at 13160 and 13491. The equal
        // raw/status path only recovers or throws, never accepts a lock.
        last_focus.raw = raw;
        last_focus.status = status;
        std::fprintf(stderr, "Camera focus raw=%04x status=%04x\n", raw, status);
        if (raw == status || __builtin_popcount(unsigned(status & 0x0fff)) != 1) {
            if (reinitialized) throw std::runtime_error("Camera focus status invalid after recovery");
            std::fprintf(stderr, "Camera focus status invalid; reinitializing once\n");
            start_stream(true);
            reinitialized = true;
            continue;
        }
        if (!(status & 0xc000)) {
            prelocked = false;
            continue;
        }
        if (!(raw & 0x8000)) {
            // Focus lock/prelock before exposure lock is stale: refocus.
            write_register(0x10a, 1);
            pause_ms(750);
            continue;
        }
        bool locked = status & 0x8000;
        if (!locked) {
            // Stock counts matching positions on re-entering prelock, not
            // every poll while that prelock state remains asserted.
            if (prelocked) continue;
            uint16_t position = status & 0x0fff;
            if (position == previous_position) ++repeated_prelocks;
            else { repeated_prelocks = 0; previous_position = position; }
            prelocked = true;
            if (repeated_prelocks != 3) continue;
            write_register(0x107, 0);
            write_register(0x108, previous_position);
            std::fprintf(stderr, "Camera accepted stable prelock position=%04x\n", previous_position);
        } else std::fprintf(stderr, "Camera reported focus lock\n");
        // Stock only preserves gain if the FPGA raw status marks it relevant.
        last_focus.gain = (raw & 0x3000) ? read_i2c(0x5d, 0x35) : 0;
        last_focus.prelocked = !locked;
        return last_focus.gain;
    }
    throw std::runtime_error("Camera did not lock focus/exposure within deadline");
}
void Camera::read_stream(std::vector<uint16_t>& output, size_t frames) {
    if (frames < 1 || frames > 6) throw std::runtime_error("Invalid camera stream burst size");
    output.resize(648 * 480 * frames);
    int received = 0;
    {
        TransferPriority priority(20);
        check(libusb_clear_halt(device_, 0x82), "Clear camera image endpoint");
        read_bytes(0x82, reinterpret_cast<uint8_t*>(output.data()), output.size() * 2, received, 5000);
    }
    if (received % 2) throw std::runtime_error("Camera stream ended within a pixel");
    output.resize(received / 2);
}
std::vector<uint16_t> Camera::read_stream(size_t frames) {
    std::vector<uint16_t> output;
    read_stream(output, frames);
    return output;
}
void Camera::capture_still(std::vector<uint16_t>& output, bool use_flash, bool stream_ready) {
    end_async_stream();
    uint16_t gain = 0;
    std::exception_ptr failure;
    try {
        if (!stream_ready) {
            start_stream();
            const auto deadline = Clock::now() + std::chrono::seconds(2);
            std::vector<uint16_t> settling;
            while (Clock::now() < deadline) read_stream(settling);
        }
        flash(use_flash);
        gain = focus();
        const uint16_t settings[][2] = {{0x1e,0x4198},{6,0x400},{0x20,0x4040},{0x22,0},{0x23,0}};
        for (auto& s : settings) write_i2c(0x5d, uint8_t(s[0]), s[1]);
        write_i2c(0x6b, 0, 0x0601);
        write_register(0x12, 1);
        pause_ms(1000);
        write_i2c(0x5d, 0x35, 12);
        // Resize/touch before trigger so allocation cannot stall the FX2 FIFO.
        output.resize(2592 * 1920);
        std::fill(output.begin(), output.end(), 0);
        int received = 0;
        {
            TransferPriority priority(100);
            check(libusb_clear_halt(device_, 0x82), "Clear camera image endpoint");
            const uint8_t trigger[]{0x08, 0x00, 0x64};
            write_bytes(trigger, sizeof(trigger));
            read_bytes(0x82, reinterpret_cast<uint8_t*>(output.data()), output.size() * 2, received, 5000);
        }
        if (size_t(received) != output.size() * 2)
            throw std::runtime_error("Incomplete camera still transfer");
    } catch (...) { failure = std::current_exception(); }
    try { flash(false); } catch (...) { if (!failure) failure = std::current_exception(); }
    try {
        if (gain) write_i2c(0x5d, 0x35, gain);
        if (stream_ready) start_stream(false);
        else stop_stream();
    } catch (...) { if (!failure) failure = std::current_exception(); }
    if (failure) std::rethrow_exception(failure);
}
std::string Camera::version() {
    // GetVersion @118e0: command 0x0f, two literal bytes (05 06 => 5.6).
    send(0x0f);
    pause_ms(10);
    uint8_t response[2]; int received = 0;
    read_bytes(0x81, response, 2, received, 1000);
    if (received != 2) throw std::runtime_error("Short camera version reply");
    return std::to_string(response[0]) + "." + std::to_string(response[1]);
}
}
