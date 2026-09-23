#pragma once
#include <windows.h>
#include <oaidl.h>

namespace EvanDragonVisibility {
// 084 507E1F..507F76: riding owner -> alpha (0,0), otherwise (255,255).
// 083 4143C6 gets alpha at COM slot E4; 4FF140..4FF164 copies TWO
// 16-byte VARIANTs onto the stack. These are VALUES, never pointers.
inline HRESULT Update(void* dragon, DWORD& model) {
    auto bytes = static_cast<BYTE*>(dragon);
    auto owner = *reinterpret_cast<BYTE**>(bytes + 0xF8);
    void* layer = *reinterpret_cast<void**>(bytes + 0x88);
    if (!owner || !layer) return S_FALSE;
    model = *reinterpret_cast<DWORD*>(owner + 0x544);
    const LONG opacity = (model / 10000 == 190 || model / 10000 == 193) ? 0 : 255;
    using GetAlpha = HRESULT(__stdcall*)(void*, void**);
    using Move = HRESULT(__stdcall*)(void*, LONG, LONG, VARIANT, VARIANT);
    using Release = ULONG(__stdcall*)(void*);
    void* alpha = nullptr;
    auto table = *reinterpret_cast<void***>(layer);
    HRESULT result = reinterpret_cast<GetAlpha>(table[0xE4 / 4])(layer, &alpha);
    if (FAILED(result) || !alpha) return FAILED(result) ? result : E_POINTER;
    auto alphaTable = *reinterpret_cast<void***>(alpha);
    VARIANT missing{};
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    result = reinterpret_cast<Move>(alphaTable[0x90 / 4])(alpha, opacity, opacity, missing, missing);
    reinterpret_cast<Release>(alphaTable[2])(alpha);
    return result;
}
}
