#include "../ezorsia/DreamCanvas.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
void Check(bool ok,int line){if(!ok){std::fprintf(stderr,"Dream canvas assertion failed at %d\n",line);std::exit(1);}}
#define Require(ok) Check((ok),__LINE__)
int wmain(int argc,wchar_t** argv){
    Require(argc==2);
    SetDllDirectoryW(argv[1]);
    HMODULE p=LoadLibraryW(L"PCOM.dll");Require(p!=nullptr);
    using Init=HRESULT(__cdecl*)();
    Require(SUCCEEDED(reinterpret_cast<Init>(GetProcAddress(p,"PcInitModule"))()));
    auto factory=reinterpret_cast<DreamCanvas::Factory>(GetProcAddress(p,"PcCreateObject"));
    const GUID iid={0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    void* source=nullptr;Require(SUCCEEDED(factory(L"Canvas",&iid,&source,nullptr)));
    using namespace DreamCanvas;
    using Create=HRESULT(__stdcall*)(void*,int,int,Variant,Variant);
    using Rect=HRESULT(__stdcall*)(void*,int,int,int,int,unsigned);
    using Pixel=HRESULT(__stdcall*)(void*,int,int,unsigned*);
    using Get=HRESULT(__stdcall*)(void*,LONG*);
    Variant zero,format;zero.type=3;format.type=3;format.value=2;
    Require(SUCCEEDED(Method<Create>(source,0x2c)(source,800,600,zero,format)));
    Require(SUCCEEDED(Method<Rect>(source,0x8c)(source,0,0,800,20,0xffffffff)));
    Require(SUCCEEDED(Method<Rect>(source,0x8c)(source,0,580,800,20,0xffffffff)));
    Require(SUCCEEDED(Method<Rect>(source,0x8c)(source,0,20,20,560,0xffffffff)));
    Require(SUCCEEDED(Method<Rect>(source,0x8c)(source,780,20,20,560,0xffffffff)));
    Require(SUCCEEDED(Method<Rect>(source,0x8c)(source,30,30,30,30,0x80ffffff)));
    Require(Scale(source,800,600,factory)==nullptr);
    for(auto size:{POINT{1024,768},POINT{1280,720},POINT{1920,1080}}){
        void* scaled=Scale(source,size.x,size.y,factory);Require(scaled!=nullptr);
        LONG w=0,h=0,x=0,y=0;
        Require(SUCCEEDED(Method<Get>(scaled,0x40)(scaled,&w)) && w==size.x);
        Require(SUCCEEDED(Method<Get>(scaled,0x48)(scaled,&h)) && h==size.y);
        Require(SUCCEEDED(Method<Get>(scaled,0x6c)(scaled,&x)) && x==size.x/2);
        Require(SUCCEEDED(Method<Get>(scaled,0x74)(scaled,&y)) && y==size.y/2);
        unsigned pixel=0;
        Require(SUCCEEDED(Method<Pixel>(scaled,0x88)(scaled,0,0,&pixel)) && pixel==0xffffffff);
        Require(SUCCEEDED(Method<Pixel>(scaled,0x88)(scaled,x,y,&pixel)) && pixel==0);
        Require(SUCCEEDED(Method<Pixel>(scaled,0x88)(scaled,MulDiv(40,w,800),MulDiv(40,h,600),&pixel)));
        unsigned sourcePixel=0;Require(SUCCEEDED(Method<Pixel>(source,0x88)(source,40,40,&sourcePixel)));
        std::printf("partial source=%08x scaled=%08x\n",sourcePixel,pixel);
        Require((sourcePixel>>24)>0 && (sourcePixel>>24)<255 && pixel==sourcePixel);
        Release(scaled);
    }
    unsigned pixel=0;Require(SUCCEEDED(Method<Pixel>(source,0x88)(source,40,40,&pixel)) && (pixel>>24)>0 && (pixel>>24)<255);
    Release(source);
    std::puts("PASS: 3 resolutions, centered origin, opaque/transparent/partial alpha, unchanged source, native 800x600 bypass");
}
