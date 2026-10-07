#pragma once
#include "MixedDyeResources.h"

namespace MonthlyShopText {
inline MixedDyeResources::Object Canvas(DreamCanvas::Factory factory,const std::wstring& text,unsigned color) {
    using namespace MixedDyeResources;
    constexpr int w=340,h=17;
    if(!factory)throw E_POINTER;
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)throw E_OUTOFMEMORY;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;
    info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    // Match the original UI's bitmap labels, without grayscale smoothing.
    HFONT font=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,GB2312_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"SimSun");
    if(!bitmap || !font){if(bitmap)DeleteObject(bitmap);if(font)DeleteObject(font);DeleteDC(dc);throw E_OUTOFMEMORY;}
    auto oldBitmap=SelectObject(dc,bitmap),oldFont=SelectObject(dc,font);
    struct Cleanup { HDC dc; HBITMAP bitmap; HFONT font; HGDIOBJ oldBitmap,oldFont;
        ~Cleanup(){SelectObject(dc,oldBitmap);SelectObject(dc,oldFont);DeleteObject(bitmap);DeleteObject(font);DeleteDC(dc);}
    } cleanup{dc,bitmap,font,oldBitmap,oldFont};
    std::memset(bits,0,w*h*4);SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
    RECT rect{0,0,w,h};DrawTextW(dc,text.c_str(),-1,&rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);GdiFlush();
    auto out=New(factory,L"Canvas",CanvasIID());
    Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(out.p,0x2c)(out.p,w,h,Integer(0),Integer(2)));
    Check(Method<HRESULT(__stdcall*)(void*,LONG)>(out.p,0x54)(out.p,2));
    // PCOM splits this 340-pixel canvas into raw tiles (256 pixels in v83).
    // GetRawCanvas(0,0) alone never owns the entire row. Keep the bounds check
    // per tile and read the corresponding region of the complete GDI bitmap.
    const int tileW=Int(out.p,0x38),tileH=Int(out.p,0x3c);
    if(tileW<=0 || tileH<=0 || tileW>4096 || tileH>4096)throw E_INVALIDARG;
    for(int ty=0;ty<h;ty+=tileH)for(int tx=0;tx<w;tx+=tileW){
        Object raw;Check(Method<HRESULT(__stdcall*)(void*,int,int,void**)>(out.p,0x34)(out.p,tx,ty,&raw.p));
        if(!raw.p)throw E_POINTER;
        LONG pitch=0;Value address;Check(Method<HRESULT(__stdcall*)(void*,LONG*,VARIANT*)>(raw.p,0x1c)(raw.p,&pitch,&address.v));
        struct Unlock{void* p;RECT r;~Unlock(){Method<HRESULT(__stdcall*)(void*,RECT*)>(p,0x20)(p,&r);}}
            unlock{raw.p,{0,0,(std::min)(tileW,w-tx),(std::min)(tileH,h-ty)}};
        if(address.v.vt!=(VT_BYREF|VT_UI4) || !address.v.byref || pitch<unlock.r.right*4 || pitch>16384)throw E_INVALIDARG;
        for(int y=0;y<unlock.r.bottom;++y)for(int x=0;x<unlock.r.right;++x){
            const unsigned source=static_cast<unsigned*>(bits)[(ty+y)*w+tx+x];
            const unsigned alpha=(std::max)({source&255,(source>>8)&255,(source>>16)&255});
            const unsigned pixel=alpha<<24 | (color&0xffffff);
            std::memcpy(static_cast<unsigned char*>(address.v.byref)+y*pitch+x*4,&pixel,4);
        }
    }
    return out;
}
}
