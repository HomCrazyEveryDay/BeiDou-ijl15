#pragma once
#include "MonthlyShopText.h"

namespace MonthlyShopTabCanvas {
using namespace MixedDyeResources;
inline void Copy(void* dst,void* src,int x,int y){
    Check(Method<HRESULT(__stdcall*)(void*,int,int,void*,VARIANT)>(dst,0x80)(dst,x,y,src,Integer(255)));
}
// Use the original shop's Tab3 edge/fill sprites at native size. No IMG writes.
template<class Load>
Object Create(Factory factory,Load load,const std::wstring& label,int width,bool selected){
    if(width<12 || width>222)throw E_INVALIDARG;
    auto out=New(factory,L"Canvas",CanvasIID());
    Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(out.p,0x2c)(out.p,width,21,Integer(0),Integer(2)));
    const std::wstring state=selected?L"1":L"0",root=L"UI/Basic.img/Tab3/";
    auto left=load(root+L"left"+state),fill=load(root+L"fill"+state),right=load(root+L"right"+state);
    const int lw=Int(left,0x40),rw=Int(right,0x40);
    if(lw+rw>width || Int(fill,0x40)!=1 || Int(fill,0x48)!=21)throw E_INVALIDARG;
    Copy(out.p,left,0,0);for(int x=lw;x<width-rw;++x)Copy(out.p,fill,x,0);Copy(out.p,right,width-rw,0);
    auto text=label;int pixels=0;size_t keep=0;
    for(wchar_t ch:text){int next=ch<128?6:12;if(pixels+next>width-6)break;pixels+=next;++keep;}
    if(keep<text.size()){
        if(keep){pixels-=text[keep-1]<128?6:12;--keep;}
        text=text.substr(0,keep)+L"\u2026";pixels+=12;
    }
    // The native bitmap labels have a subtle dark shadow under white glyphs.
    auto shadow=MonthlyShopText::Canvas(factory,text,selected?0x9c3855:0x686868);
    auto foreground=MonthlyShopText::Canvas(factory,text,0xffffff);
    const int x=(width-pixels)/2;
    Copy(out.p,shadow.p,x+1,6);Copy(out.p,foreground.p,x,5);
    return out;
}
}
