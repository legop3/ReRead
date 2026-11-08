#!/usr/bin/env python3
"""Compare ReRead RGB against stock ELF image routines, offline in a builder.

Requirements: extracted analysis/partitions/root.img, debugfs, Python Pillow, an existing Docker
builder with g++-multilib, i386 libjpeg development files and en_US.UTF-8 locale.
No camera hardware access; old USB discovery is overridden with empty results.
Stock proprietary artifacts stay under ignored build/antix/camera-oracle/.
"""
from array import array
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
WORK=ROOT/'build/antix/camera-oracle'
ORACLE_SOURCE = r'''
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <iterator>
#include <dlfcn.h>
#include <cstdint>
extern "C" { int usb_find_busses(){return 0;} int usb_find_devices(){return 0;} void* usb_busses=nullptr; }
int main(int argc,char**argv){
 void* u=dlopen("libusb-0.1.so.4",RTLD_LAZY|RTLD_GLOBAL); if(!u){puts(dlerror());return 1;}
 ((void(*)())dlsym(u,"usb_init"))();void* h=dlopen("libcolbert-camera.so",RTLD_LAZY|RTLD_GLOBAL);if(!h){puts(dlerror());return 2;}
 using Still=void(*)(std::vector<uint16_t>&,std::vector<unsigned char>&,unsigned&,unsigned&,float);
 using Stream=void(*)(std::vector<uint16_t>&,std::vector<unsigned char>&,unsigned&,unsigned&,bool);
 FILE* input=fopen(argv[2],"rb");fseek(input,0,SEEK_END);size_t n=ftell(input)/2;rewind(input);std::vector<uint16_t> raw(n);fread(raw.data(),2,n,input);fclose(input);std::vector<unsigned char> out;unsigned w=0,height=0;
 try{if(argv[1][0]=='s'&&argv[1][1]=='t')((Still)dlsym(h,"_ZN7colbert6camera17ProcessStillImageERSt6vectorItSaItEERS1_IhSaIhEERjS8_f"))(raw,out,w,height,argc>4?atof(argv[4]):0.0f);else ((Stream)dlsym(h,"_ZN7colbert6camera18ProcessStreamImageERSt6vectorItSaItEERS1_IhSaIhEERjS8_b"))(raw,out,w,height,false);}catch(...){puts("stock exception");return 3;}
 printf("%ux%u %u bytes\n",w,height,unsigned(out.size()));std::ofstream f(argv[3],std::ios::binary);f<<"P6\n"<<w<<' '<<height<<"\n255\n";f.write((char*)out.data(),out.size());
}
'''

NATIVE_SOURCE = r'''
#include "/work/antix/camera_processing.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
int main(int argc,char**argv) {
 FILE*f=fopen(argv[2],"rb");fseek(f,0,SEEK_END);size_t n=ftell(f)/2;rewind(f);std::vector<uint16_t> raw(n);fread(raw.data(),2,n,f);fclose(f);
 bool still=argv[1][0]=='s'&&argv[1][1]=='t';
 auto im=reread::process_frame(raw,still?2592:648,still?1920:480,still,argc>4);
 std::ofstream out(argv[3],std::ios::binary);out<<"P6\n"<<im.width<<' '<<im.height<<"\n255\n";out.write((char*)im.rgb.data(),im.rgb.size());
 auto jpg=reread::jpeg(im,85,960);FILE*j=fopen((std::string(argv[3])+".jpg").c_str(),"wb");fwrite(jpg.data(),1,jpg.size(),j);fclose(j);
}
'''

ORIENTATION_NATIVE_SOURCE = r'''
#include "/work/antix/camera_processing.hpp"
#include <cstdio>
int main(){
 reread::Image im;im.width=32;im.height=32;im.rgb.resize(32*32*3);
 for(int y=0;y<32;++y)for(int x=0;x<32;++x)for(int c=0;c<3;++c)im.rgb[(y*32+x)*3+c]=y<16?(c==0?10:c==1?60:210):(c==0?170:c==1?80:20);
 for(int mode=0;mode<3;++mode){auto out=reread::jpeg(im,100,mode==2?16:0,mode>0,mode>0);char path[64];snprintf(path,sizeof(path),"orientation-%d.jpg",mode);FILE*f=fopen(path,"wb");fwrite(out.data(),1,out.size(),f);fclose(f);}
}
'''

def run(*args):
    subprocess.run(args,check=True,cwd=ROOT)


def rgb(path):
    magic,dimensions,depth,data=path.read_bytes().split(b'\n',3)
    assert magic==b'P6' and depth==b'255'
    w,h=map(int,dimensions.split())
    return w,h,data[:w*h*3]


def extract_libraries():
    image=ROOT/'analysis/partitions/root.img'
    if not image.exists():
        raise SystemExit('First extract the stock root partition (analysis/extract-stock.py).')
    paths=['usr/lib/libboost_filesystem-gcc42-1_34_1.so.1.34.1',
           'usr/lib/libboost_iostreams-gcc42-1_34_1.so.1.34.1',
           'usr/lib/libboost_thread-gcc42-mt-1_34_1.so.1.34.1',
           'usr/lib/libboost_date_time-gcc42-1_34_1.so.1.34.1',
           'usr/lib/libxml2.so.2.6.32','usr/lib/libz.so.1.2.3.3',
           'lib/libbz2.so.1.0.4','lib/libusb-0.1.so.4.4.4',
           'lib/libusb-1.0.so.0.0.0']
    aliases={'libxml2.so.2':'libxml2.so.2.6.32','libz.so.1':'libz.so.1.2.3.3',
             'libbz2.so.1.0':'libbz2.so.1.0.4',
             'libusb-0.1.so.4':'libusb-0.1.so.4.4.4',
             'libusb-1.0.so.0':'libusb-1.0.so.0.0.0'}
    for name in paths:
        dst=WORK/Path(name).name
        if not dst.exists():run('debugfs','-R',f'dump /{name} {dst}',str(image))
    for alias,target in aliases.items():
        if not (WORK/alias).exists():(WORK/alias).symlink_to(target)
    for name in ['libcolbert-camera.so','libcolbert-common.so']:
        source=ROOT/'analysis/stock-root/opt/colbert/lib'/name
        if not source.exists():
            run('debugfs','-R',f'dump /opt/colbert/lib/{name} {WORK/name}',str(image))
        else:shutil.copyfile(source,WORK/name)


def synthetic(w,h,path):
    values=array('H',(100+(x*19+y*7+((x&1)+2*(y&1))*251)%3500
                      for y in range(h) for x in range(w)))
    for y in range(h):values[y*w]|=0x1000
    values[0]|=0x8000
    path.write_bytes(values.tobytes())


def stream_frame(source,path):
    values=array('H');values.frombytes(source.read_bytes())
    n=648*480
    for start in range(len(values)-n+1):
        if not values[start]&0x8000:continue
        candidate=values[start:start+n]
        if all((v&0x9000)==((0x1000 if i%648==0 else 0)|(0x8000 if i==0 else 0))
               for i,v in enumerate(candidate)):
            path.write_bytes(candidate.tobytes());return
    raise ValueError('No complete aligned live-view frame')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--container',default='intelreader-validator')
    parser.add_argument('--still',type=Path,help='Optional complete 2592x1920 raw fixture')
    parser.add_argument('--stream',type=Path,help='Optional live USB burst (first complete frame selected)')
    args=parser.parse_args();WORK.mkdir(parents=True,exist_ok=True)
    extract_libraries()
    (WORK/'oracle.cpp').write_text(ORACLE_SOURCE)
    (WORK/'native.cpp').write_text(NATIVE_SOURCE)
    def docker(command):run('docker','exec',args.container,'sh','-c',
                            'cd /work/build/antix/camera-oracle && '+command)
    docker('g++ -m32 -O2 -rdynamic -D_GLIBCXX_USE_CXX11_ABI=0 oracle.cpp -ldl -o oracle && '
           'g++ -m32 -O3 -march=bonnell -Wall -Wextra -Werror -std=c++17 -c /work/antix/camera_processing.cpp -o processing.o && '
           'g++ -m32 -O2 -std=c++17 native.cpp processing.o -ljpeg -o native')
    cases=[]
    for mode,w,h in [('still',2592,1920),('live',648,480)]:
        name='synthetic-'+mode;path=WORK/(name+'.raw');synthetic(w,h,path)
        cases.append((name,mode,path,False))
    if args.still:
        path=WORK/'real-still.raw';shutil.copyfile(args.still.resolve(),path)
        cases.append(('real-still','still',path,False))
        cases.append(('real-document','still',path,True))
    else:
        cases.append(('synthetic-document','still',WORK/'synthetic-still.raw',True))
    if args.stream:
        path=WORK/'real-live.raw';stream_frame(args.stream.resolve(),path)
        cases.append(('real-live','live',path,False))
    results=[]
    for name,mode,path,document in cases:
        saturation=' -0.6' if document else ''
        native_flag=' document' if document else ''
        docker(f'LD_LIBRARY_PATH=. ./oracle {mode} {path.name} {name}-stock.ppm{saturation} && '
               f'./native {mode} {path.name} {name}-native.ppm{native_flag}')
        w,h,stock=rgb(WORK/(name+'-stock.ppm'))
        ww,hh,new=rgb(WORK/(name+'-native.ppm'))
        equal=(w,h,stock)==(ww,hh,new)
        entry=dict(name=name,width=w,height=h,byte_exact=equal,
                   input_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                   stock_rgb_sha256=hashlib.sha256(stock).hexdigest(),
                   native_rgb_sha256=hashlib.sha256(new).hexdigest())
        results.append(entry);print(name, f'{w}x{h}', 'BYTE-EXACT' if equal else 'DIFFERS',flush=True)
    # Check output layout separately from byte-exact processing. JPEG's
    # lossy color quantization permits a two-level difference on flat patches.
    from PIL import Image
    (WORK/'orientation-native.cpp').write_text(ORIENTATION_NATIVE_SOURCE)
    docker('g++ -m32 -O2 -std=c++17 orientation-native.cpp processing.o -ljpeg -o orientation-native && ./orientation-native')
    for mode in range(3):
        image=Image.open(WORK/f'orientation-{mode}.jpg')
        actual=[image.getpixel((image.width//2,image.height//4)),
                image.getpixel((image.width//2,image.height*3//4))]
        expected=[(10,60,210),(170,80,20)] if mode==0 else [(20,80,170),(210,60,10)]
        if max(abs(x-y) for a,b in zip(actual,expected) for x,y in zip(a,b))>2:
            raise AssertionError(f'JPEG row/channel layout differs: {mode} {actual}')
    print('JPEG RGB/default, flipped BGR/full, flipped BGR/scaled: PASS',flush=True)
    report={'source_sha256':hashlib.sha256((ROOT/'antix/camera_processing.cpp').read_bytes()).hexdigest(),
            'stock_library_sha256':hashlib.sha256((WORK/'libcolbert-camera.so').read_bytes()).hexdigest(),
            'results':results,'jpeg_layouts':'PASS'}
    (WORK/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    if not all(x['byte_exact'] for x in results):raise SystemExit(1)

if __name__=='__main__':main()
