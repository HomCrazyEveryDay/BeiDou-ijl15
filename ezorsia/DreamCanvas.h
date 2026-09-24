#pragma once
#include <windows.h>

// IWzCanvas ABI verified against the shipped Canvas.dll, without a game session.
namespace DreamCanvas {
struct Variant { WORD type=0, r1=0, r2=0, r3=0; LONG value=0, high=0; };
static_assert(sizeof(Variant)==16, "WZ VARIANT ABI");
using Factory = HRESULT(__cdecl*)(const wchar_t*,const GUID*,void**,void*);
template<class F> F Method(void* object, unsigned offset) {
    return reinterpret_cast<F>((*reinterpret_cast<void***>(object))[offset/4]);
}
inline void Release(void* object) {
    if(object) Method<ULONG(__stdcall*)(void*)>(object,8)(object);
}
inline void* Scale(void* source,int width,int height,Factory create) {
    using Get = HRESULT(__stdcall*)(void*,LONG*);
    LONG oldWidth=0,oldHeight=0,x=0,y=0;
    if(!source || !create || width<800 || height<600 || width>7680 || height>4320
        || (width==800 && height==600)) return nullptr;
    if(FAILED(Method<Get>(source,0x40)(source,&oldWidth))
        || FAILED(Method<Get>(source,0x48)(source,&oldHeight))
        || oldWidth!=800 || oldHeight!=600
        || FAILED(Method<Get>(source,0x6c)(source,&x))
        || FAILED(Method<Get>(source,0x74)(source,&y))) return nullptr;
    static const GUID iid={0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    void* target=nullptr;
    if(FAILED(create(L"Canvas",&iid,&target,nullptr)) || !target) return nullptr;
    Variant magnification,format; magnification.type=3;format.type=3;format.value=2;
    using Create=HRESULT(__stdcall*)(void*,int,int,Variant,Variant);
    using Copy=HRESULT(__stdcall*)(void*,int,int,void*,int,int,int,int,int,int,int,Variant);
    using Put=HRESULT(__stdcall*)(void*,LONG);
    HRESULT hr=Method<Create>(target,0x2c)(target,width,height,magnification,format);
    if(SUCCEEDED(hr)) hr=Method<Copy>(target,0x84)(target,0,0,source,-1,width,height,0,0,800,600,Variant{});
    if(SUCCEEDED(hr)) hr=Method<Put>(target,0x70)(target,MulDiv(x,width,800));
    if(SUCCEEDED(hr)) hr=Method<Put>(target,0x78)(target,MulDiv(y,height,600));
    if(FAILED(hr)){Release(target);return nullptr;}
    return target;
}
}
