#!/usr/bin/env python3
"""Verify JPEG RGB layout and 180-degree rotation with asymmetric color targets.
Run on the build host; uses the standard i386 builder and Pillow for inspection.
"""
from pathlib import Path
import json
import subprocess
from PIL import Image

root = Path(__file__).resolve().parent.parent
work = root / 'build/antix/orientation-check'
work.mkdir(exist_ok=True)
(work / 'check.cpp').write_text(r'''
#include "/work/antix/camera_processing.hpp"
#include <fstream>
int main(){
    reread::Image live{64,48,std::vector<unsigned char>(64*48*3)},stock=live;
    const unsigned char colors[4][3]={{220,20,30},{10,210,40},{20,40,220},{230,210,20}};
    for(int y=0;y<48;y++)for(int x=0;x<64;x++)for(int c=0;c<3;c++){
        auto color=colors[(y>=24)*2+(x>=32)][c];
        live.rgb[(y*64+x)*3+c]=color;
        stock.rgb[((47-y)*64+x)*3+2-c]=color;
    }
    for(int mode=0;mode<3;mode++){
        auto bytes=reread::jpeg(mode==0?live:stock,100,0,mode!=0,mode!=0,mode==2);
        std::ofstream file(std::to_string(mode)+".jpg",std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    }
}
''')
subprocess.run(['docker','exec','intelreader-validator','sh','-c',
    'cd /work/build/antix/orientation-check && g++ -m32 -std=c++17 -O3 -march=bonnell check.cpp '
    '/work/antix/camera_processing.cpp -ljpeg -o check && ./check'],check=True)
colors=[(220,20,30),(10,210,40),(20,40,220),(230,210,20)]
results=[]
for mode in range(3):
    image=Image.open(work/f'{mode}.jpg')
    actual=[image.getpixel(p) for p in [(16,12),(48,12),(16,36),(48,36)]]
    expected=colors[::-1] if mode==2 else colors
    assert max(abs(a-b) for color,want in zip(actual,expected) for a,b in zip(color,want))<=3,(mode,actual)
    results.append(dict(mode=mode,pixels=actual,passed=True))
(work/'result.json').write_text(json.dumps(results,indent=2)+'\n')
print('RGB live, normalized stock still, and normalized + 180° still: PASS')
