// Research only: execute the Reader owner's unmodified stock library. No USB
// enumeration stubs, EEPROM updater, or FPGA programmer is linked or invoked.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <vector>
#include <fstream>
#include <chrono>
#include <stdexcept>
#include <string>
#include <exception>
using Words = std::vector<uint16_t>;
using Clock = std::chrono::steady_clock;
template<class Function> Function resolve(void* library, const char* name) {
    dlerror(); void* function = dlsym(library, name);
    const char* error = dlerror();
    if (error) throw std::runtime_error(std::string(name) + ": " + error);
    return reinterpret_cast<Function>(function);
}
int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr,"Usage: hardware output.raw\n"); return 1; }
    void* usb = dlopen("libusb-0.1.so.4", RTLD_NOW | RTLD_GLOBAL);
    if (!usb) { std::fprintf(stderr,"%s\n",dlerror());return 2; }
    resolve<void(*)()>(usb,"usb_init")();
    void* library = dlopen("libcolbert-camera.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!library) { std::fprintf(stderr,"%s\n",dlerror());return 2; }
    auto start = resolve<void(*)()>(library,"_ZN7colbert6camera6detail5StartEv");
    auto stop = resolve<void(*)()>(library,"_ZN7colbert6camera6detail4StopEv");
    auto set_mode = resolve<void(*)(int,uint16_t&)>(library,"_ZN7colbert6camera6detail7SetModeENS1_8ModeEnumERt");
    auto stream = resolve<void(*)(Words&)>(library,"_ZN7colbert6camera6detail18CaptureStreamImageERSt6vectorItSaItEE");
    auto write_sensor = resolve<void(*)(uint8_t,uint16_t)>(library,"_ZN7colbert6camera6sensor5WriteEht");
    auto read_fpga = resolve<uint16_t(*)(uint16_t)>(library,"_ZN7colbert6camera4fpga4ReadEt");
    // i386 SysV ABI passes the non-trivial boost::function by invisible
    // reference. CaptureStillImage @13df7 only reads its first word and skips
    // the callback when null; the caller owns destruction. A zeroed opaque
    // object provides an empty callback without depending on new Boost ABI.
    auto capture = resolve<void(*)(Words&,void*)>(library,"_ZN7colbert6camera6detail17CaptureStillImageERSt6vectorItSaItEEN5boost8functionIFvvESaIvEEE");
    uint16_t gain = 0; bool started = false;
    auto cleanup = [&]() {
        if (!started) return;
        try { if (gain) write_sensor(0x35,gain); } catch (...) {}
        uint16_t unused = 0;
        try { set_mode(1,unused); } catch (...) {}
        try { set_mode(0,unused); } catch (...) {}
        try { stop(); } catch (...) {}
        started = false;
    };
    try {
        std::fprintf(stderr,"Stock Start\n");
        started = true; start();
        set_mode(1,gain);
        Words burst; size_t bytes = 0; unsigned bursts = 0;
        auto settling_start = Clock::now();
        auto deadline = settling_start + std::chrono::seconds(3);
        while (Clock::now() < deadline) { stream(burst); bytes += burst.size()*2; ++bursts; }
        double settle_seconds = std::chrono::duration<double>(Clock::now()-settling_start).count();
        uint16_t before_raw = read_fpga(0x1000), before_status = read_fpga(0x1005);
        std::fprintf(stderr,"Stock focus entry raw=%04x status=%04x\n",before_raw,before_status);
        auto focus_start = Clock::now();
        set_mode(2,gain);
        double focus_seconds = std::chrono::duration<double>(Clock::now()-focus_start).count();
        uint16_t raw = read_fpga(0x1000), status = read_fpga(0x1005);
        write_sensor(0x35,12);
        Words still(2592*1920,0);
        alignas(16) unsigned char empty_callback[64]{};
        auto capture_start = Clock::now();
        capture(still, empty_callback);
        double capture_seconds = std::chrono::duration<double>(Clock::now()-capture_start).count();
        std::ofstream output(argv[1],std::ios::binary);
        output.write(reinterpret_cast<char*>(still.data()),still.size()*2);
        if (!output) throw std::runtime_error("Cannot save stock raw still");
        cleanup();
        std::printf("{\"stock\":true,\"stream_bursts\":%u,\"stream_bytes\":%zu,\"stream_seconds\":%.4f,\"before_raw\":%u,\"before_status\":%u,\"focus_raw\":%u,\"focus_status\":%u,\"gain\":%u,\"focus_transition_seconds\":%.4f,\"capture_seconds\":%.4f,\"still_bytes\":%zu}\n",bursts,bytes,settle_seconds,before_raw,before_status,raw,status,gain,focus_seconds,capture_seconds,still.size()*2);
    } catch (const std::exception& error) {
        std::fprintf(stderr,"Stock hardware exception: %s\n",error.what());cleanup();return 3;
    } catch (...) { std::fprintf(stderr,"Stock hardware exception (old stock C++ type)\n");cleanup();return 3; }
    // Intentionally retain libraries until normal process teardown: stock
    // globals and their destructors refer to one another.
    return 0;
}
