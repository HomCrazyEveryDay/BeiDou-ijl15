#pragma once
#include "MixedDyeResources.h"
#include "CrashReporter.h"
#include <mutex>

// The v83 chair loader (93C7C3 -> 941417 -> ResMan::GetObject) requests
// effect/effect2 properties, then enumerates their canvases. Resolve modern
// same-IMG pixel references before that enumeration, without editing shared art.
namespace ChairImageLinks {
using namespace MixedDyeResources;
using Getter = HRESULT(__stdcall*)(void*, BSTR, VARIANT, VARIANT, VARIANT*);
constexpr wchar_t RootPath[] = L"Item/Install/0301.img/03015759";
constexpr wchar_t LinkPrefix[] = L"03015759/";

inline bool Match(BSTR path, std::wstring& suffix) {
    if (!path || SysStringLen(path) > 256) return false;
    const wchar_t* p = path;
    if (*p == L'/') ++p;
    if (wcsncmp(p, L"Data/", 5) == 0) p += 5;
    constexpr size_t length = _countof(RootPath) - 1;
    if (wcsncmp(p, RootPath, length) || (p[length] && p[length] != L'/')) return false;
    suffix = p[length] ? p + length + 1 : L"";
    return true;
}

inline Value Read(void* root, const std::wstring& path) {
    Value value;
    value.v.vt = VT_UNKNOWN;
    value.v.punkVal = static_cast<IUnknown*>(root);
    value.v.punkVal->AddRef();
    size_t begin = 0;
    while (begin < path.size()) {
        auto end = path.find(L'/', begin);
        if (end == std::wstring::npos) end = path.size();
        auto key = path.substr(begin, end - begin);
        if (key.empty() || key == L"." || key == L"..") throw E_INVALIDARG;
        auto canvas = Query(value.v, CanvasIID());
        auto property = canvas.p ? CanvasProperty(canvas.p) : Query(value.v, PropertyIID());
        if (!property.p) throw E_INVALIDARG;
        Value next;
        Get(property.p, key, &next.v);
        value = std::move(next);
        begin = end + 1;
    }
    return value;
}

inline Object PixelSource(void* root, void* canvas, unsigned hops = 0) {
    if (hops >= 24) throw E_INVALIDARG;
    auto metadata = CanvasProperty(canvas);
    Value outside, inside;
    Get(metadata.p, L"_outlink", &outside.v);
    Get(metadata.p, L"_inlink", &inside.v);
    if (!Empty(outside.v)) throw E_INVALIDARG; // Cross-IMG imports need their own verified dependencies.
    if (Empty(inside.v)) {
        static_cast<IUnknown*>(canvas)->AddRef();
        return Object(canvas);
    }
    if (inside.v.vt != VT_BSTR || !inside.v.bstrVal || SysStringLen(inside.v.bstrVal) > 256)
        throw E_INVALIDARG;
    constexpr size_t prefixLength = _countof(LinkPrefix) - 1;
    if (wcsncmp(inside.v.bstrVal, LinkPrefix, prefixLength)) throw E_INVALIDARG;
    auto target = Read(root, inside.v.bstrVal + prefixLength);
    auto next = Query(target.v, CanvasIID());
    if (!next.p) throw E_INVALIDARG;
    return PixelSource(root, next.p, hops + 1);
}

struct Limits { size_t bytes = 0; unsigned nodes = 0, canvases = 0; };
inline Object CopyPixels(void* source, void* metadata, Factory factory, Limits& limits) {
    const int w = Int(source, 0x40), h = Int(source, 0x48);
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) throw E_INVALIDARG;
    const size_t bytes = size_t(w) * h * 4;
    if (limits.bytes + bytes > 2 * 1024 * 1024 || limits.canvases >= 64) throw E_OUTOFMEMORY;
    auto out = New(factory, L"Canvas", CanvasIID());
    Check(Method<HRESULT(__stdcall*)(void*, int, int, VARIANT, VARIANT)>(out.p, 0x2c)(out.p, w, h, Integer(0), Integer(2)));
    Check(Method<HRESULT(__stdcall*)(void*, LONG)>(out.p, 0x54)(out.p, 2));
    const int tw = Int(out.p, 0x38), th = Int(out.p, 0x3c);
    if (tw <= 0 || th <= 0 || tw > 4096 || th > 4096) throw E_INVALIDARG;
    for (int ty = 0; ty < h; ty += th) for (int tx = 0; tx < w; tx += tw) {
        Object raw;
        Check(Method<HRESULT(__stdcall*)(void*, int, int, void**)>(out.p, 0x34)(out.p, tx, ty, &raw.p));
        if (!raw.p) throw E_POINTER;
        LONG pitch = 0;
        Value address;
        Check(Method<HRESULT(__stdcall*)(void*, LONG*, VARIANT*)>(raw.p, 0x1c)(raw.p, &pitch, &address.v));
        struct Unlock {
            void* raw; RECT rect;
            ~Unlock() { Method<HRESULT(__stdcall*)(void*, RECT*)>(raw, 0x20)(raw, &rect); }
        } unlock{raw.p, {0, 0, (std::min)(tw, w - tx), (std::min)(th, h - ty)}};
        if (address.v.vt != (VT_BYREF | VT_UI4) || !address.v.byref || pitch < unlock.rect.right * 4 || pitch > 16384)
            throw E_INVALIDARG;
        auto pixels = static_cast<unsigned char*>(address.v.byref);
        for (int y = 0; y < unlock.rect.bottom; ++y) for (int x = 0; x < unlock.rect.right; ++x) {
            const unsigned color = Pixel(source, tx + x, ty + y, w, h);
            std::memcpy(pixels + y * pitch + x * 4, &color, 4);
        }
    }
    // A link supplies pixels only. Delay, origin, alpha, z and all other frame
    // properties belong to the referring frame, not to its pixel source.
    auto from = CanvasProperty(metadata), to = CanvasProperty(out.p);
    for (const auto& key : Names(from.p)) {
        if (key == L"_inlink" || key == L"_outlink") continue;
        Value value;
        Get(from.p, key, &value.v);
        Put(to.p, key, value.v);
    }
    using WriteInt = HRESULT(__stdcall*)(void*, LONG);
    Check(Method<WriteInt>(out.p, 0x70)(out.p, Int(metadata, 0x6c)));
    Check(Method<WriteInt>(out.p, 0x78)(out.p, Int(metadata, 0x74)));
    limits.bytes += bytes;
    ++limits.canvases;
    return out;
}

inline Object Clone(void* root, void* property, Factory factory, Limits& limits, unsigned depth = 0) {
    if (depth > 16 || ++limits.nodes > 512) throw E_INVALIDARG;
    auto out = New(factory, L"Property", PropertyIID());
    for (const auto& key : Names(property)) {
        if (++limits.nodes > 512) throw E_INVALIDARG;
        Value value;
        Get(property, key, &value.v);
        auto canvas = Query(value.v, CanvasIID());
        if (canvas.p) {
            auto metadata = CanvasProperty(canvas.p);
            Value link;
            Get(metadata.p, L"_inlink", &link.v);
            if (!Empty(link.v)) {
                auto source = PixelSource(root, canvas.p);
                auto resolved = CopyPixels(source.p, canvas.p, factory, limits);
                PutObject(out.p, key, resolved.p);
            } else Put(out.p, key, value.v);
        } else {
            auto child = Query(value.v, PropertyIID());
            if (child.p) {
                auto cloned = Clone(root, child.p, factory, limits, depth + 1);
                PutObject(out.p, key, cloned.p);
            } else Put(out.p, key, value.v);
        }
    }
    return out;
}

struct Cache { std::recursive_mutex mutex; void* manager = nullptr; Object image; };
inline Cache& Cached() { static auto* cache = new Cache; return *cache; } // COM may unload before ijl15.
inline HRESULT GetObject(Getter native, Factory factory, void* manager, BSTR path, VARIANT a, VARIANT b, VARIANT* result) {
    static thread_local bool building = false;
    if (building) return native(manager, path, a, b, result);
    try {
        std::wstring suffix;
        if (!Match(path, suffix)) return native(manager, path, a, b, result);
        if (!result) return E_POINTER;
        auto& cache = Cached();
        std::lock_guard<std::recursive_mutex> lock(cache.mutex);
        if (!cache.image.p || cache.manager != manager) {
            struct Guard { bool& flag; Guard(bool& b) : flag(b) { flag = true; } ~Guard() { flag = false; } } guard(building);
            BSTR name = SysAllocString(RootPath);
            if (!name) throw E_OUTOFMEMORY;
            Value original;
            const auto hr = native(manager, name, a, b, &original.v);
            SysFreeString(name);
            Check(hr);
            auto property = Query(original.v, PropertyIID());
            if (!property.p) throw E_NOINTERFACE;
            Limits limits;
            auto replacement = Clone(property.p, property.p, factory, limits);
            DreamCanvas::Release(cache.image.p);
            cache.image.p = replacement.detach();
            cache.manager = manager;
            CrashReporter::RecordEvent("resource.chair", "item=3015759 inlinks=%u pixel_bytes=%zu", limits.canvases, limits.bytes);
        }
        auto value = Read(cache.image.p, suffix);
        VariantInit(result);
        Check(VariantCopy(result, &value.v));
        return S_OK;
    } catch (...) {
        static LONG reports = 0;
        if (InterlockedIncrement(&reports) <= 8)
            CrashReporter::RecordEvent("resource.chair", "item=3015759 resolve_failed; using original resource");
        return native(manager, path, a, b, result);
    }
}
}
