#include "../ezorsia/IncubationCanvas.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
void Check(bool ok, int line) { if (!ok) { std::fprintf(stderr,"FAIL line %d\n",line); std::exit(1); } }
#define Require(ok) Check((ok),__LINE__)
int wmain(int argc, wchar_t** argv) {
    using namespace DreamCanvas;
    using IncubationCanvas::Part;
    Require(argc == 2);
    SetDllDirectoryW(argv[1]);
    auto module = LoadLibraryW(L"PCOM.dll"); Require(module != nullptr);
    using Init = HRESULT(__cdecl*)();
    Require(SUCCEEDED(reinterpret_cast<Init>(GetProcAddress(module,"PcInitModule"))()));
    auto factory = reinterpret_cast<Factory>(GetProcAddress(module,"PcCreateObject"));
    Require(factory != nullptr);
    Require(IncubationCanvas::Identify(nullptr) == Part::None);
    Require(IncubationCanvas::Identify(L"Effect/Direction4.img/effect/incubation/back/0") == Part::Background);
    Require(IncubationCanvas::Identify(L"Effect/Direction4.img/effect/incubation/light") == Part::Foreground);
    Require(IncubationCanvas::Identify(L"Effect/Direction4.img/effect/incubation/light/0") == Part::Foreground);
    Require(IncubationCanvas::Identify(L"Effect/Direction4.img/effect/incubation/lightning") == Part::None);
    Require(IncubationCanvas::Identify(L"Effect/BasicEff.img/LevelUp") == Part::None);
    const GUID iid = {0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    using Create = HRESULT(__stdcall*)(void*,int,int,Variant,Variant);
    using Rect = HRESULT(__stdcall*)(void*,int,int,int,int,unsigned);
    using Pixel = HRESULT(__stdcall*)(void*,int,int,unsigned*);
    using Get = HRESULT(__stdcall*)(void*,LONG*);
    using Put = HRESULT(__stdcall*)(void*,LONG);
    // Real frame sizes/origins plus a negative origin to test cropped-frame placement.
    for (auto frame : {RECT{800,600,400,300}, RECT{460,485,207,235}, RECT{756,570,-12,285}}) {
        void* source = nullptr;
        Require(SUCCEEDED(factory(L"Canvas",&iid,&source,nullptr)) && source);
        Variant zero,format; zero.type=3; format.type=3; format.value=2;
        Require(SUCCEEDED(Method<Create>(source,0x2c)(source,frame.left,frame.top,zero,format)));
        Require(SUCCEEDED(Method<Put>(source,0x70)(source,frame.right)));
        Require(SUCCEEDED(Method<Put>(source,0x78)(source,frame.bottom)));
        Require(SUCCEEDED(Method<Rect>(source,0x8c)(source,0,0,20,20,0xffffffff)));
        Require(SUCCEEDED(Method<Rect>(source,0x8c)(source,30,30,30,30,0x80ffffff)));
        const Part part = frame.left == 800 ? Part::Background : Part::Foreground;
        Require(!IncubationCanvas::Scale(source,800,600,part,factory));
        Require(!IncubationCanvas::Scale(source,1920,1080,Part::None,factory));
        Require(!IncubationCanvas::Scale(source,9000,1080,part,factory));
        for (auto size : {POINT{1024,768},POINT{1280,720},POINT{1920,1080},POINT{2560,1440},POINT{3440,1440}}) {
            void* scaled = IncubationCanvas::Scale(source,size.x,size.y,part,factory); Require(scaled);
            const int numerator = part == Part::Background ? size.x : (size.x*600 <= size.y*800 ? size.x : size.y);
            const int denominator = part == Part::Background ? 800 : (size.x*600 <= size.y*800 ? 800 : 600);
            LONG w=0,h=0,x=0,y=0;
            Require(SUCCEEDED(Method<Get>(scaled,0x40)(scaled,&w)) && w==MulDiv(frame.left,numerator,denominator));
            Require(SUCCEEDED(Method<Get>(scaled,0x48)(scaled,&h)) && h==(part==Part::Background ? size.y : MulDiv(frame.top,numerator,denominator)));
            Require(SUCCEEDED(Method<Get>(scaled,0x6c)(scaled,&x)) && x==MulDiv(frame.right,numerator,denominator));
            Require(SUCCEEDED(Method<Get>(scaled,0x74)(scaled,&y)) && y==(part==Part::Background ? size.y/2 : MulDiv(frame.bottom,numerator,denominator)));
            unsigned pixel=0,original=0;
            Require(SUCCEEDED(Method<Pixel>(scaled,0x88)(scaled,0,0,&pixel)) && pixel==0xffffffff);
            Require(SUCCEEDED(Method<Pixel>(scaled,0x88)(scaled,w/2,h/2,&pixel)) && pixel==0);
            Require(SUCCEEDED(Method<Pixel>(source,0x88)(source,40,40,&original)));
            Require(SUCCEEDED(Method<Pixel>(scaled,0x88)(scaled,MulDiv(40,w,frame.left),MulDiv(40,h,frame.top),&pixel)) && pixel==original);
            Release(scaled);
        }
        LONG origin=0;
        Require(SUCCEEDED(Method<Get>(source,0x6c)(source,&origin)) && origin==frame.right);
        Release(source);
    }
    std::puts("PASS: 5 resolutions, 3 frame geometries, aspect ratio, signed origins, alpha, source preservation, path isolation");
}
