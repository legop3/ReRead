#pragma once
#include <cstdint>
#include <vector>
namespace reread {
// Recovered stock byte layout: still BGR/bottom-up, stream RGB/top-down.
// Use JPEG flip/swap flags for still output; raw oracle comparisons retain it.
struct Image { int width=0,height=0; std::vector<unsigned char> rgb; };
Image process_frame(const std::vector<uint16_t>& frame,int width,int height,bool still,bool document=false);
std::vector<unsigned char> jpeg(const Image&,int quality,int max_width=0,bool flip_vertical=false,bool swap_rb=false,bool rotate_180=false);
}
