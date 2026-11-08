#pragma once
#include <cstddef>
#include <cstdint>

// Stable C boundary for the small Python HTTP/control layer. USB ownership,
// acquisition, focus, framing, image processing and encoding stay in C++.
extern "C" {
struct ReaderCameraStats {
    uint64_t received, displayed, dropped, damaged;
    double capture_fps, preview_fps;
    int running, width, height;
};
struct ReaderCameraPhoto {
    int width, height;
    unsigned raw, status, gain, prelocked;
};
void* reader_camera_create(char* error, size_t error_size);
void reader_camera_shutdown(void* engine);
void reader_camera_destroy(void* engine);
int reader_camera_frame(void* engine, uint64_t after, unsigned char** jpeg,
                        size_t* size, uint64_t* sequence, char* error, size_t error_size);
int reader_camera_rgb_frame(void* engine, uint64_t after, unsigned char** pixels,
    size_t* size, uint64_t* sequence, int* width, int* height, char* error, size_t error_size);
void reader_camera_free(void* bytes);
int reader_camera_capture(void* engine, int flash, int document, int rotation, const char* photo_path,
                          const char* preview_path, ReaderCameraPhoto* photo,
                          char* error, size_t error_size);
void reader_camera_stats(void* engine, ReaderCameraStats* stats);
}
