#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>
struct libusb_context;
struct libusb_device_handle;

namespace reread {
// The hardware has one owner. Callers serialize all methods, including close.
// Firmware loading is performed by the platform before constructing Camera.
class Camera {
public:
    struct Focus { uint16_t raw = 0, status = 0, gain = 0; bool prelocked = false; };
    Focus last_focus;
    Camera();
    ~Camera();
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    void start_stream(bool initialize = true);
    void stop_stream();
    void begin_async_stream();
    void read_async_stream(std::vector<uint16_t>& output);
    void end_async_stream() noexcept;
    std::vector<uint16_t> read_stream(size_t frames = 3);
    void read_stream(std::vector<uint16_t>& output, size_t frames = 3);
    void capture_still(std::vector<uint16_t>& output, bool flash, bool stream_ready);
    void close() noexcept;
    uint16_t read_register(uint16_t reg);
    void write_register(uint16_t reg, uint16_t value);
    uint16_t read_i2c(uint8_t address, uint8_t reg);
    void write_i2c(uint8_t address, uint8_t reg, uint16_t value);
    uint16_t focus(double timeout_seconds = 10);
    std::string version();
private:
    struct AsyncStream;
    AsyncStream* async_ = nullptr;
    libusb_context* context_ = nullptr;
    libusb_device_handle* device_ = nullptr;
    bool claimed_ = false;
    bool active_ = false;
    void initialize();
    void set_streaming(bool enabled);
    void flash(bool enabled);
    void send(uint8_t command, std::initializer_list<uint8_t> payload = {},
              uint8_t subcommand = 0, uint8_t address = 0);
    void write_bytes(const uint8_t* bytes, size_t count);
    void read_bytes(uint8_t endpoint, uint8_t* bytes, size_t count,
                    int& received, unsigned timeout);
};
}
