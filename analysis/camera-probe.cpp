// Research CLI using the same native hardware backend as the platform.
// Pause the shared service with reader-camera suspend before running this.
#include "../antix/camera_hardware.hpp"
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
int main(int argc, char** argv) {
    if (argc < 3 || argc > 4 ||
        (std::string(argv[1]) != "--still" && std::string(argv[1]) != "--stream")) {
        std::fprintf(stderr, "Usage: camera-probe --still output.raw [--flash]\n"
                             "       camera-probe --stream output.raw\n");
        return 1;
    }
    try {
        reread::Camera camera;
        std::vector<uint16_t> raw;
        if (std::string(argv[1]) == "--still")
            camera.capture_still(raw, argc == 4 && std::string(argv[3]) == "--flash", false);
        else {
            camera.start_stream(); camera.read_stream(raw, 3); camera.stop_stream();
        }
        std::ofstream output(argv[2], std::ios::binary);
        if (!output.write(reinterpret_cast<const char*>(raw.data()), raw.size()*2))
            throw std::runtime_error("Cannot save raw camera frame");
        std::printf("{\"bytes\":%zu,\"focus_raw\":%u,\"focus_status\":%u,\"gain\":%u,\"prelocked\":%s}\n",
                    raw.size()*2, camera.last_focus.raw, camera.last_focus.status,
                    camera.last_focus.gain, camera.last_focus.prelocked?"true":"false");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what()); return 2;
    }
}
