#pragma once
#include "MixedDyeResources.h"
#include "CrashReporter.h"
#include <mutex>

// Preserve the v83 0301.img namespace, including CItemInfo's root-then-child
// lookup. Split buckets override individual chairs without rewriting the large
// legacy IMG, changing the global Item formatter, or copying any canvas pixels.
namespace SplitChairResources {
using namespace MixedDyeResources;
using Getter = HRESULT(__stdcall*)(void*, BSTR, VARIANT, VARIANT, VARIANT*);
constexpr wchar_t LegacyRoot[] = L"Item/Install/0301.img";

inline bool Match(BSTR path, std::wstring& suffix) {
    if (!path || SysStringLen(path) > 512) return false;
    const wchar_t* p = path;
    if (*p == L'/') ++p;
    if (wcsncmp(p, L"Data/", 5) == 0) p += 5;
    constexpr size_t length = _countof(LegacyRoot) - 1;
    if (wcsncmp(p, LegacyRoot, length) || (p[length] && p[length] != L'/')) return false;
    suffix = p[length] ? p + length + 1 : L"";
    return true;
}

inline int ChairId(const std::wstring& name) {
    if (name.size() != 8 || name.compare(0, 4, L"0301")) return 0;
    int id = 0;
    for (wchar_t c : name) {
        if (c < L'0' || c > L'9') return 0;
        id = id * 10 + c - L'0';
    }
    return id;
}

inline Value Load(Getter native, void* manager, const std::wstring& path, VARIANT a, VARIANT b) {
    BSTR name = SysAllocString(path.c_str());
    if (!name) throw E_OUTOFMEMORY;
    Value value;
    const auto hr = native(manager, name, a, b, &value.v);
    SysFreeString(name);
    // Absent buckets are normal during incremental migration. Other failures
    // must not silently become a cached "no split chairs" result.
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == STG_E_FILENOTFOUND) return Value();
    Check(hr);
    return value;
}

struct Cache {
    std::recursive_mutex mutex;
    void* manager = nullptr;
    bool ready = false;
    Object root;
    std::map<int, std::wstring> locations;
};
// PCOM may unload before DLL statics at process exit; keep the same bounded,
// process-lifetime ownership policy as the other resource adapters.
inline Cache& Cached() { static auto* cache = new Cache; return *cache; }

inline void Build(Cache& cache, Getter native, Factory factory, void* manager, VARIANT a, VARIANT b) {
    std::map<int, std::wstring> locations;
    std::vector<std::pair<std::wstring, Value>> chairs;
    for (int group = 3010; group <= 3019; ++group) {
        wchar_t path[64];
        swprintf_s(path, L"Item/Install/%05d.img", group);
        auto value = Load(native, manager, path, a, b);
        if (Empty(value.v)) continue;
        auto bucket = Query(value.v, PropertyIID());
        if (!bucket.p) throw E_NOINTERFACE;
        for (const auto& key : Names(bucket.p)) {
            const int id = ChairId(key);
            if (!id || id / 1000 != group) throw E_INVALIDARG;
            Value child;
            Get(bucket.p, key, &child.v);
            if (!Query(child.v, PropertyIID()).p) throw E_NOINTERFACE;
            locations.emplace(id, path);
            chairs.emplace_back(key, std::move(child));
        }
    }
    Object merged;
    if (!chairs.empty()) {
        auto legacy = Load(native, manager, LegacyRoot, a, b);
        auto original = Query(legacy.v, PropertyIID());
        if (!original.p) throw E_NOINTERFACE;
        auto created = New(factory, L"Property", PropertyIID());
        merged.p = created.detach();
        for (const auto& key : Names(original.p)) {
            Value child;
            Get(original.p, key, &child.v);
            Put(merged.p, key, child.v);
        }
        for (auto& chair : chairs) Put(merged.p, chair.first, chair.second.v);
    }
    DreamCanvas::Release(cache.root.p);
    cache.root.p = merged.detach();
    cache.locations = std::move(locations);
    cache.manager = manager;
    cache.ready = true;
}

inline HRESULT GetObject(Getter native, Factory factory, void* manager, BSTR path,
                         VARIANT a, VARIANT b, VARIANT* result) {
    static thread_local bool building = false;
    std::wstring suffix;
    if (building || !Match(path, suffix)) return native(manager, path, a, b, result);
    if (!result) return E_POINTER;
    try {
        auto& cache = Cached();
        std::lock_guard<std::recursive_mutex> lock(cache.mutex);
        if (!cache.ready || cache.manager != manager) {
            struct Guard { bool& flag; Guard(bool& f) : flag(f) { flag = true; } ~Guard() { flag = false; } } guard(building);
            Build(cache, native, factory, manager, a, b);
        }
        if (!cache.root.p) return native(manager, path, a, b, result);
        if (suffix.empty()) {
            VariantInit(result);
            result->vt = VT_UNKNOWN;
            result->punkVal = static_cast<IUnknown*>(cache.root.p);
            result->punkVal->AddRef();
            return S_OK;
        }
        const int id = ChairId(suffix.substr(0, suffix.find(L'/')));
        const auto found = cache.locations.find(id);
        if (found == cache.locations.end()) return native(manager, path, a, b, result);
        // Let PCOM retain native UOL/canvas/deep-property semantics in the real
        // split IMG. Never redirect a missing child back to stale legacy data.
        BSTR redirected = SysAllocString((found->second + L"/" + suffix).c_str());
        if (!redirected) return E_OUTOFMEMORY;
        const auto hr = native(manager, redirected, a, b, result);
        SysFreeString(redirected);
        return hr;
    } catch (...) {
        static LONG reports = 0;
        if (InterlockedIncrement(&reports) <= 8)
            CrashReporter::RecordEvent("resource.chair-split", "merge_failed; legacy lookup preserved");
        return native(manager, path, a, b, result);
    }
}
}
