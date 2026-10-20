#define NOMINMAX
#include "../ezorsia/MixedDyeResources.h"
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#pragma comment(lib, "ole32.lib")

static void Require(bool ok, const char* text) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", text); std::exit(1); }
}
#ifdef RESOURCE_HOST_ALLOCATOR
// Export the same public allocator contract as the game. PCOM discovers it
// itself; no executable mapping, patching, or game initialization is needed.
static HANDLE resourceHeap;
static bool failAllocation;
static unsigned releases;
static auto& Blocks() { static auto* blocks = new std::unordered_map<void*, size_t>; return *blocks; }
extern "C" void* __stdcall ZtlTaskMemAllocImp(size_t size) {
    if (failAllocation) return nullptr;
    void* value = HeapAlloc(resourceHeap, 0, size);
    if (value) Blocks()[value] = size;
    return value;
}
extern "C" void __stdcall ZtlTaskMemFreeImp(void* value) {
    if (!value) return;
    Require(Blocks().erase(value) == 1, "resource free uses its owning heap exactly once");
    Require(HeapFree(resourceHeap, 0, value) != FALSE, "resource heap free");
    ++releases;
}
extern "C" void* __stdcall ZtlTaskMemReallocImp(void* value, size_t size) {
    if (!value) return ZtlTaskMemAllocImp(size);
    if (!size) { ZtlTaskMemFreeImp(value); return nullptr; }
    Require(Blocks().count(value) == 1, "resource realloc ownership");
    void* replacement = HeapReAlloc(resourceHeap, 0, value, size);
    if (replacement) { Blocks().erase(value); Blocks()[replacement] = size; }
    return replacement;
}
#pragma comment(linker, "/EXPORT:ZtlTaskMemAllocImp=_ZtlTaskMemAllocImp@4")
#pragma comment(linker, "/EXPORT:ZtlTaskMemFreeImp=_ZtlTaskMemFreeImp@4")
#pragma comment(linker, "/EXPORT:ZtlTaskMemReallocImp=_ZtlTaskMemReallocImp@8")
static bool Owns(BSTR text) { return text && Blocks().count(reinterpret_cast<BYTE*>(text) - sizeof(UINT)) != 0; }
#endif

struct Reference : IUnknown {
    ULONG references = 1;
    HRESULT __stdcall QueryInterface(REFIID, void** out) override { *out = this; AddRef(); return S_OK; }
    ULONG __stdcall AddRef() override { return ++references; }
    ULONG __stdcall Release() override { return --references; }
};

static void CheckOwnership() {
    using namespace MixedDyeResources;
    VARIANT input{};
    input.vt = VT_BSTR;
    const wchar_t embedded[] = {L'A', L'\0', L'B'};
    input.bstrVal = SysAllocStringLen(embedded, 3);
    Require(input.bstrVal != nullptr, "system input string");
    {
        Value first;
        Check(ResourceVariant::Copy(&first.v, &input));
        Require(first.v.bstrVal != input.bstrVal && SysStringLen(first.v.bstrVal) == 3
            && !std::memcmp(first.v.bstrVal, embedded, sizeof(embedded)), "copy preserves embedded NUL and length");
#ifdef RESOURCE_HOST_ALLOCATOR
        Require(Owns(first.v.bstrVal), "copy returned to game uses host allocator");
        const size_t baseline = Blocks().size();
        Value second;
        Check(ResourceVariant::Copy(&second.v, &input));
        second = std::move(first);
        Require(first.v.vt == VT_EMPTY && Blocks().size() == baseline, "move releases old storage once");
        Check(ResourceVariant::Copy(&second.v, &second.v));
        Require(Blocks().size() == baseline, "self copy preserves storage");
        failAllocation = true;
        Value failed;
        Require(ResourceVariant::Copy(&failed.v, &input) == E_OUTOFMEMORY && failed.v.vt == VT_EMPTY,
            "allocation failure leaves an empty result");
        failAllocation = false;
#else
        Value second(std::move(first));
        Require(first.v.vt == VT_EMPTY && SysStringLen(second.v.bstrVal) == 3, "standalone move");
#endif
    }
    VariantClear(&input); // Caller-owned inputs always retain the system pairing.
    {
        Reference object;
        Value first;
        first.v.vt = VT_UNKNOWN; first.v.punkVal = &object; object.AddRef();
        Value second;
        Check(ResourceVariant::Copy(&second.v, &first.v));
        Require(object.references == 3, "interface copy retains reference");
        Check(ResourceVariant::Clear(&first.v));
        Check(ResourceVariant::Clear(&second.v));
        Require(object.references == 1, "interface cleanup releases references");
    }
    {
        BSTR borrowed = SysAllocString(L"borrowed");
        Value value;
        value.v.vt = VT_BSTR | VT_BYREF; value.v.pbstrVal = &borrowed;
        Check(ResourceVariant::Clear(&value.v));
        Require(!wcscmp(borrowed, L"borrowed"), "BYREF cleanup preserves borrowed input");
        SysFreeString(borrowed);
        value.v.vt = VT_BSTR; value.v.bstrVal = nullptr;
        Value copy; Check(ResourceVariant::Copy(&copy.v, &value.v));
        Require(copy.v.vt == VT_BSTR && copy.v.bstrVal == nullptr, "null BSTR copy");
    }
}

int wmain(int argc, wchar_t** argv) {
    Require(argc == 2, "component directory");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#ifdef RESOURCE_HOST_ALLOCATOR
    resourceHeap = HeapCreate(0, 0, 0); Require(resourceHeap != nullptr, "private resource heap");
#endif
    CheckOwnership();
    Require(SetDllDirectoryW(argv[1]) != FALSE, "component search directory");
    MixedDyeResources::Check(CoInitialize(nullptr));
    auto module = LoadLibraryW(L"PCOM.dll"); Require(module != nullptr, "PCOM load");
    using Init = HRESULT(__cdecl*)();
    auto init = reinterpret_cast<Init>(GetProcAddress(module, "PcInitModule"));
    std::atexit([]{if(auto term=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(GetModuleHandleW(L"PCOM.dll"),"PcTermModule")))term();}); // Shut PCOM down like BeiDou.exe; skipping it faults in PCOM DLL detach.
    Require(init != nullptr, "PCOM init export"); MixedDyeResources::Check(init());
    auto factory = reinterpret_cast<DreamCanvas::Factory>(GetProcAddress(module, "PcCreateObject"));
    Require(factory != nullptr, "PCOM factory");
    {
        using namespace MixedDyeResources;
        auto root = New(factory, L"Property", PropertyIID());
        auto info = New(factory, L"Property", PropertyIID());
        VARIANT text{}; text.vt = VT_BSTR; text.bstrVal = SysAllocString(L"Hr");
        Put(info.p, L"islot", text); VariantClear(&text);
        PutObject(root.p, L"info", info.p);
#ifdef RESOURCE_HOST_ALLOCATOR
        { Value returned; Get(info.p, L"islot", &returned.v);
          Require(Owns(returned.v.bstrVal), "real PCOM selected host allocator exports"); }
        const auto baseline = Blocks().size();
#endif
        for (unsigned i = 0; i < 10000; ++i) {
            CheckCategory(root.p, 40902);
            auto names = Names(info.p);
            Require(names.size() == 1 && names[0] == L"islot", "resource enumeration result");
            Value returned; Get(info.p, L"islot", &returned.v);
            Value copy; Check(ResourceVariant::Copy(&copy.v, &returned.v));
            Require(!wcscmp(copy.v.bstrVal, L"Hr"), "native resource return copy");
            ResourceVariant::String owned;
            owned.value = copy.v.bstrVal; VariantInit(&copy.v);
        }
#ifdef RESOURCE_HOST_ALLOCATOR
        Require(Blocks().size() == baseline, "repeated resource reads do not leak host allocations");
#endif
    }
#ifdef RESOURCE_HOST_ALLOCATOR
    Require(HeapValidate(resourceHeap, 0, nullptr) != FALSE, "resource heap remains intact");
    std::printf("PASS host allocator: 10000 category/enumeration/copy/string cleanup cycles, releases=%u\n", releases);
#else
    std::puts("PASS system allocator: 10000 category/enumeration/copy/string cleanup cycles");
#endif
    CoUninitialize();
}
