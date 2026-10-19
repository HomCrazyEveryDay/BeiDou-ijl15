#pragma once
#include "DreamCanvas.h"

namespace WardrobeClip {
inline bool SupportsPlanes(void* device){
    // D3DCAPS8: 53 DWORD-sized fields; MaxUserClipPlanes is at byte A4.
    // SDK layout: mingw-w64-headers/include/d3d8caps.h (D3D8, not D3D9).
    DWORD caps[53]{};
    return device && SUCCEEDED(DreamCanvas::Method<HRESULT(__stdcall*)(void*,void*)>(device,0x1c)(device,caps)) && caps[0xa4/4]>=4;
}
// Direct3D 8 fixed-function user planes. Coordinates are WZ world coordinates,
// before the native view transform. Neither viewport nor projection is changed.
struct DeviceState {
    void* device=nullptr;DWORD enabled=0;float planes[4][4]{};bool captured=false;
    bool Begin(void* target,float left,float top,float right,float bottom) {
        using DreamCanvas::Method;device=target;
        if(FAILED(Method<HRESULT(__stdcall*)(void*,int,DWORD*)>(device,0xcc)(device,152,&enabled)))return false;
        for(unsigned i=0;i<4;i++)if(FAILED(Method<HRESULT(__stdcall*)(void*,unsigned,float*)>(device,0xc4)(device,i,planes[i])))return false;
        captured=true;
        const float clip[4][4]={{1,0,0,-left},{-1,0,0,right},{0,1,0,-top},{0,-1,0,bottom}};
        for(unsigned i=0;i<4;i++)if(FAILED(Method<HRESULT(__stdcall*)(void*,unsigned,const float*)>(device,0xc0)(device,i,clip[i])))return false;
        return SUCCEEDED(Method<HRESULT(__stdcall*)(void*,int,DWORD)>(device,0xc8)(device,152,enabled|15));
    }
    void Restore() {
        if(!captured)return;using DreamCanvas::Method;
        for(unsigned i=0;i<4;i++)Method<HRESULT(__stdcall*)(void*,unsigned,const float*)>(device,0xc0)(device,i,planes[i]);
        Method<HRESULT(__stdcall*)(void*,int,DWORD)>(device,0xc8)(device,152,enabled);captured=false;
    }
};
bool Install(void* graphics);
void Uninstall();
void SetRoot(void* layer,int left,int top,int width,int height);
void SetVisible(bool visible);
// Permanently retire this preview's native layers before releasing its owner.
// Hides each descendant and severs origin/overlay references, including layers
// still retained by an animation. Returns the number of retired layers.
unsigned RetireTree();
bool Failed();
}
