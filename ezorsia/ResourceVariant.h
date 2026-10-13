#pragma once
#include <windows.h>
#include <oleauto.h>
#include <cstring>
#include <limits>

// PCOM resource outputs use the host's Ztl allocator when its exports exist.
// A BSTR still has the usual four-byte length prefix, but its storage then
// belongs to the game pool, not OleAut32. Inputs created with SysAllocString
// remain caller-owned and must still be released with SysFreeString.
namespace ResourceVariant {
struct Allocator {
    using Allocate = void* (__stdcall*)(size_t);
    using Free = void (__stdcall*)(void*);
    Allocate allocate;
    Free free;
};
inline const Allocator& HostAllocator() {
    static const Allocator allocator = [] {
        const auto host = GetModuleHandleW(nullptr);
        return Allocator{
            reinterpret_cast<Allocator::Allocate>(GetProcAddress(host, "ZtlTaskMemAllocImp")),
            reinterpret_cast<Allocator::Free>(GetProcAddress(host, "ZtlTaskMemFreeImp"))};
    }();
    return allocator;
}
inline void FreeString(BSTR value) noexcept {
    if (!value) return;
    const auto release = HostAllocator().free;
    if (release) release(reinterpret_cast<BYTE*>(value) - sizeof(UINT));
    else SysFreeString(value); // Standalone PCOM uses the Automation allocator.
}
struct String {
    BSTR value = nullptr;
    String() = default;
    String(const String&) = delete;
    String& operator=(const String&) = delete;
    ~String() { FreeString(value); }
};
inline HRESULT Clear(VARIANT* value) noexcept {
    if (!value) return E_INVALIDARG;
    if (value->vt != VT_BSTR) return VariantClear(value);
    FreeString(value->bstrVal);
    VariantInit(value);
    return S_OK;
}
// Copies returned to native callers must use the same allocator as PCOM.
// VariantCopy would silently create an OleAut32 BSTR for a game-pool owner.
inline HRESULT Copy(VARIANT* destination, const VARIANT* source) {
    if (!destination || !source) return E_INVALIDARG;
    if (destination == source) return S_OK;
    const HRESULT cleared = Clear(destination);
    if (FAILED(cleared)) return cleared;
    if (source->vt == VT_BSTR && !source->bstrVal) {
        destination->vt = VT_BSTR;
        destination->bstrVal = nullptr;
        return S_OK;
    }
    if (source->vt != VT_BSTR || !HostAllocator().allocate)
        return VariantCopy(destination, const_cast<VARIANT*>(source));
    BSTR text = nullptr;
    if (source->bstrVal) {
        const UINT bytes = SysStringByteLen(source->bstrVal);
        if (bytes > (std::numeric_limits<UINT>::max)() - sizeof(UINT) - sizeof(wchar_t))
            return E_OUTOFMEMORY;
        auto storage = static_cast<BYTE*>(HostAllocator().allocate(bytes + sizeof(UINT) + sizeof(wchar_t)));
        if (!storage) return E_OUTOFMEMORY;
        std::memcpy(storage, &bytes, sizeof(bytes));
        std::memcpy(storage + sizeof(UINT), source->bstrVal, bytes);
        std::memset(storage + sizeof(UINT) + bytes, 0, sizeof(wchar_t));
        text = reinterpret_cast<BSTR>(storage + sizeof(UINT));
    }
    destination->vt = VT_BSTR;
    destination->bstrVal = text;
    return S_OK;
}
}
