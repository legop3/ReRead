// ReRead camera image processing recovered from libcolbert-camera.so.
// Stock functions: lens shading 0x16dc0, AWB 0x16f20, Bayer 0x17200,
// sharpen 0x16f90, histogram/gain 0x17090/0x170d0, still 0x18760,
// stream 0x18e80. No percentile gamma curve or guessed color matrix.
#include "camera_processing.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <setjmp.h>
#include <jpeglib.h>

namespace reread {
namespace {
using Histogram=std::array<int,64>;

void stock_gain(std::vector<unsigned char>& rgb,const Histogram& hist) {
    // Stock uses floor(byte_count/100)*2 and *99, and four-value bins.
    const int unit=static_cast<int>(rgb.size()/100);
    int lo=0,hi=0,count=hist[0];
    while(lo<64 && count<unit*2) { ++lo; if(lo<64)count+=hist[lo]; }
    count=hist[0];
    while(hi<64 && count<unit*99) { ++hi; if(hi<64)count+=hist[hi]; }
    const int black=lo*4;
    const volatile float scale=255.0f/static_cast<float>(hi*4-black-1);
    std::array<unsigned char,256> lut{};
    for(int v=0;v<256;++v) {
        const volatile float value=static_cast<float>(std::max(0,v-black))*scale;
        // Stock saturates >255, otherwise converts float with truncation.
        lut[v]=value>255.0f?255:static_cast<unsigned char>(static_cast<int>(value));
    }
    for(auto& value:rgb)value=lut[value];
}

Image stock_bayer(const std::vector<uint16_t>& raw,int w,int h,
                  const std::array<double,3>& gains,int pattern,bool document) {
    Image out;out.width=w-2;out.height=h-2;
    out.rgb.resize(static_cast<size_t>(out.width)*out.height*3);
    const std::array<unsigned,3> coef={static_cast<unsigned>(gains[0]*256),
        static_cast<unsigned>(gains[1]*256),static_cast<unsigned>(gains[2]*256)};
    // Stock bayer table: 0={Gh,R,B,Gv}, 1={B,Gv,Gh,R},
    // 2={R,Gh,Gv,B}, 3={Gv,B,R,Gh}. Gh/Gv describe red neighbors.
    static constexpr int table[4][4]={{0,1,2,3},{2,3,0,1},{1,0,3,2},{3,2,1,0}};
    for(int y=1;y<h-1;++y) {
        auto* dst=out.rgb.data()+static_cast<size_t>(y-1)*out.width*3;
        const auto* top=raw.data()+static_cast<size_t>(y-1)*w;
        const auto* mid=top+w;const auto* bot=mid+w;
        for(int x=1;x<w-1;++x) {
            const unsigned diagonal=(top[x-1]+top[x+1]+bot[x-1]+bot[x+1])/4;
            const unsigned cross=(top[x]+mid[x-1]+mid[x+1]+bot[x])/4;
            unsigned r,g,b;
            switch(table[pattern][(y&1)*2+(x&1)]) {
                case 0:r=(mid[x-1]+mid[x+1])/2;g=(diagonal+mid[x])/2;b=(top[x]+bot[x])/2;break;
                case 1:r=mid[x];g=cross;b=diagonal;break;
                case 2:r=diagonal;g=cross;b=mid[x];break;
                default:r=(top[x]+bot[x])/2;g=(diagonal+mid[x])/2;b=(mid[x-1]+mid[x+1])/2;break;
            }
            unsigned values[3]={((r>>4)*coef[0])>>8,((g>>4)*coef[1])>>8,((b>>4)*coef[2])>>8};
            if(document) {
                // detail::ProcessPicture imageType=0 passes -0.6f; stock
                // truncates saturation*256 to -153, then uses signed >>8.
                for(int c: {0,2})values[c]=static_cast<uint16_t>(values[c]+
                    ((static_cast<int>(values[c])-static_cast<int>(values[1]))*-153>>8));
            }
            for(int c=0;c<3;++c)*dst++=static_cast<unsigned char>(std::min(255u,values[c]));
        }
    }
    return out;
}

Image stock_sharpen(const Image& input,Histogram& hist) {
    static constexpr int kernel[3][3]={{-1,-2,-1},{-2,20,-2},{-1,-2,-1}};
    Image out;out.width=input.width-2;out.height=input.height-2;
    out.rgb.resize(static_cast<size_t>(out.width)*out.height*3);hist.fill(0);
    for(int y=1;y<input.height-1;++y) {
        auto* dst=out.rgb.data()+static_cast<size_t>(y-1)*out.width*3;
        for(int x=1;x<input.width-1;++x)for(int c=0;c<3;++c) {
            int total=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
                total+=kernel[dy+1][dx+1]*input.rgb[(static_cast<size_t>(y+dy)*input.width+x+dx)*3+c];
            const int value=total<=7?0:std::min(255,total>>3);
            *dst++=static_cast<unsigned char>(value);++hist[value>>2];
        }
    }
    return out;
}

struct JpegError { jpeg_error_mgr base;jmp_buf jump;char message[JMSG_LENGTH_MAX]; };
void jpeg_error(j_common_ptr context) {
    auto* err=reinterpret_cast<JpegError*>(context->err);
    context->err->format_message(context,err->message);longjmp(err->jump,1);
}

std::vector<unsigned char> encode_jpeg(const unsigned char* pixels,int width,int height,int quality,bool flip_vertical,bool swap_rb) {
    jpeg_compress_struct compressor{};JpegError error{};
    compressor.err=jpeg_std_error(&error.base);error.base.error_exit=jpeg_error;
    unsigned char* buffer=nullptr;unsigned long size=0;
    if(setjmp(error.jump)) { jpeg_destroy_compress(&compressor);std::free(buffer);throw std::runtime_error(error.message); }
    jpeg_create_compress(&compressor);jpeg_mem_dest(&compressor,&buffer,&size);
    compressor.image_width=width;compressor.image_height=height;compressor.input_components=3;compressor.in_color_space=swap_rb?JCS_EXT_BGR:JCS_RGB;
    jpeg_set_defaults(&compressor);jpeg_set_quality(&compressor,std::clamp(quality,1,100),TRUE);jpeg_start_compress(&compressor,TRUE);
    while(compressor.next_scanline<compressor.image_height) {
        const size_t y=flip_vertical?height-1-compressor.next_scanline:compressor.next_scanline;
        auto* row=const_cast<unsigned char*>(pixels+y*width*3);
        jpeg_write_scanlines(&compressor,&row,1);
    }
    jpeg_finish_compress(&compressor);std::vector<unsigned char> result(buffer,buffer+size);
    jpeg_destroy_compress(&compressor);std::free(buffer);return result;
}
} // namespace

Image process_frame(const std::vector<uint16_t>& frame,int width,int height,bool still,bool document) {
    if(width<6||height<6||width>2592||height>1920||
       frame.size()!=static_cast<size_t>(width)*height)
        throw std::runtime_error("Camera frame dimensions or length are invalid");
    std::vector<uint16_t> pixels(frame.size());
    std::array<double,4> sums{};
    constexpr double lens_coefficient=2.43523e-7; // stock rodata 0x21788
    for(int y=0;y<height;++y) {
        const int yy=y-height/2;
        const long double row_factor=1.0L+static_cast<long double>(lens_coefficient)*yy*yy;
        for(int x=0;x<width;++x) {
            const size_t index=static_cast<size_t>(y)*width+x;
            const unsigned flags=frame[index]&0x9000;
            const unsigned expected=(x==0?0x1000:0)|(index==0?0x8000:0);
            if(flags!=expected)throw std::runtime_error("The camera lost image rows; retry capture");
            unsigned value=frame[index]&0xfff;
            if(still) {
                const int xx=x-width/2;
                value=std::min(4095u,static_cast<unsigned>(value*(row_factor+static_cast<long double>(lens_coefficient)*xx*xx)));
            }
            pixels[index]=static_cast<uint16_t>(value);
            sums[(y&1)*2+(x&1)]+=value;
        }
    }
    // Exact stock converter indices: still=1, stream=3 (0x21740/44).
    // Their final RGB/BGR interpretation belongs to the output boundary.
    const int pattern=still?1:3;
    const double red=still?sums[3]:sums[2];
    const double green=still?(sums[1]+sums[2])*0.5:(sums[0]+sums[3])*0.5;
    const double blue=still?sums[0]:sums[1];
    const double average=(red+green+blue)/3.0;
    std::array<double,3> gains{red>0?average/red:1.0,green>0?average/green:1.0,blue>0?average/blue:1.0};
    Image out=stock_bayer(pixels,width,height,gains,pattern,still&&document);
    Histogram histogram{};
    if(still)out=stock_sharpen(out,histogram);
    else for(const auto v:out.rgb)++histogram[v>>2];
    stock_gain(out.rgb,histogram);
    return out;
}

std::vector<unsigned char> jpeg(const Image& input,int quality,int max_width,bool flip_vertical,bool swap_rb) {
    if(input.width<=0||input.height<=0||input.rgb.size()!=static_cast<size_t>(input.width)*input.height*3)
        throw std::runtime_error("Invalid RGB image");
    const int width=max_width>0?std::min(max_width,input.width):input.width;
    const int height=std::max(1,static_cast<int>(static_cast<int64_t>(input.height)*width/input.width));
    // Preview scaling uses an area average; full-resolution encoding is direct.
    std::vector<unsigned char> scaled;
    const unsigned char* pixels=input.rgb.data();
    if(width!=input.width) {
        scaled.resize(static_cast<size_t>(width)*height*3);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const int x0=x*input.width/width,x1=(x+1)*input.width/width;
            const int y0=y*input.height/height,y1=(y+1)*input.height/height;
            unsigned totals[3]={0,0,0};
            for(int yy=y0;yy<y1;++yy)for(int xx=x0;xx<x1;++xx)for(int c=0;c<3;++c)
                totals[c]+=input.rgb[(static_cast<size_t>(yy)*input.width+xx)*3+c];
            const unsigned count=(x1-x0)*(y1-y0);
            for(int c=0;c<3;++c)scaled[(static_cast<size_t>(y)*width+x)*3+c]=totals[c]/count;
        }
        pixels=scaled.data();
    }
    return encode_jpeg(pixels,width,height,quality,flip_vertical,swap_rb);
}
} // namespace reread
