#include <windows.h>
#include <oleauto.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <xmmintrin.h>
#include <string>
#pragma comment(lib, "ole32.lib")

static int reports = 0;
static bool disturbFloatingState = false;
namespace CrashReporter {
void RecordEvent(const char*, const char*, ...) {
    ++reports;
    if (disturbFloatingState) _mm_setcsr(_mm_getcsr() ^ 0x2000);
}
}
#include "ResourceReadGuardsUnderTest.h"

static void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void MapClient(const wchar_t* path) {
    FILE* file = nullptr;
    Require(_wfopen_s(&file, path, L"rb") == 0, "open EXE");
    BYTE header[4096];
    Require(fread(header, 1, sizeof(header), file) == sizeof(header), "read PE");
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(header);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(header + dos->e_lfanew);
    Require(nt->FileHeader.TimeDateStamp == 0x4B7C15C9 && nt->OptionalHeader.SizeOfImage == 0xA94000,
        "supported EXE version");
    auto base = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(0x30400000),
        nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Require(base == reinterpret_cast<void*>(0x30400000), "map native image");
    memcpy(base, header, sizeof(header));
    auto section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        fseek(file, section[i].PointerToRawData, SEEK_SET);
        Require(fread(base + section[i].VirtualAddress, 1, section[i].SizeOfRawData, file)
            == section[i].SizeOfRawData, "read section");
    }
    fclose(file);
    // The client's entry point is never called. Only the two selected native
    // continuations execute, with allocation/hash helpers replaced below.
}
static void Jump(DWORD site, void* target) {
    auto code = reinterpret_cast<BYTE*>(site);
    code[0] = 0xe9;
    *reinterpret_cast<DWORD*>(code + 1) = reinterpret_cast<DWORD>(target) - site - 5;
    FlushInstructionCache(GetCurrentProcess(), code, 5);
}

static BYTE part[0x24], data[0x34], frame[0x100];
static void* framePointer = frame + 0x80;
static std::vector<DWORD> slotKeys;
static void* __fastcall FindSlot(void*, void*, const DWORD*, void**) { return nullptr; }
static void* __fastcall SetSlot(void*, void*, const DWORD* key, void**) {
    slotKeys.push_back(*key); return nullptr;
}
__declspec(naked) static void ReturnFromSort() { __asm { ret } }
static DWORD avatarSite = 0x00401D3E;
static void RunAvatar() {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp, framePointer
        lea esi, part
        xor ebx, ebx
        xor edx, edx
        call dword ptr [avatarSite]
        pop edi
        pop esi
        pop ebx
        pop ebp
    }
}
static void AvatarCase(BSTR* derived, BSTR* original, unsigned count, bool skipped) {
    memset(part, 0, sizeof(part)); memset(data, 0, sizeof(data)); memset(frame, 0, sizeof(frame));
    *reinterpret_cast<void**>(part + 0x20) = data;
    *reinterpret_cast<int*>(part + 0x14) = 1;
    *reinterpret_cast<BSTR**>(data + 0x14) = derived;
    *reinterpret_cast<BSTR**>(data + 0x0c) = original;
    slotKeys.clear();
    const unsigned csr = _mm_getcsr();
    disturbFloatingState = true;
    RunAvatar();
    disturbFloatingState = false;
    Require(_mm_getcsr() == csr, "avatar helper preserves MXCSR");
    Require(slotKeys.size() == count, "native loop visits every two-character slot");
    Require(*reinterpret_cast<int*>(part + 0x14) == (skipped ? 0 : 1), "part visibility");
    Require(*reinterpret_cast<BSTR**>(data + 0x14) == derived, "borrowed fallback not cached");
}

struct Object : IUnknown {
    ULONG refs = 0;
    unsigned releases = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override { *out = this; AddRef(); return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { Require(refs != 0, "COM reference underflow"); ++releases; return --refs; }
};
static Object property, fallbackCanvas, nativeCanvas;
struct Item { unsigned refs; } item;
static int calculates = 0, caches = 0, itemReleases = 0;
static DWORD cachedChecksum = 0, cachedItem = 0;
static void* __fastcall AddItemRef(void* ref, void*) {
    auto value = *reinterpret_cast<Item**>(static_cast<BYTE*>(ref) + 4);
    Require(value == &item, "normal CRC receives item"); ++value->refs; return ref;
}
static void __fastcall ReleaseItemRef(void* ref, void*, int) {
    auto value = *reinterpret_cast<Item**>(static_cast<BYTE*>(ref) + 4);
    Require(value && value->refs, "item ZRef release"); --value->refs; ++itemReleases;
}
static DWORD __fastcall Calculate(void*, void*, DWORD, Item* value) {
    Require(value == &item && value->refs == 2, "normal CRC owns copied ZRef");
    --value->refs; ++calculates; return 0x12345678;
}
static void* __fastcall Cache(void*, void*, const DWORD* key, const DWORD* value) {
    ++caches; cachedItem = *key; cachedChecksum = *value; return nullptr;
}
__declspec(naked) static void ReturnChecksum() {
    __asm {
        mov eax, [ebp - 10h]
        ret
    }
}
static DWORD itemSite = 0x005DAD7E;
static DWORD itemResult;
static DWORD RunItem() {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp, framePointer
        xor ebx, ebx
        xor esi, esi
        xor edi, edi
        mov ecx, 31415926h
        call dword ptr [itemSite]
        mov itemResult, eax
        pop edi
        pop esi
        pop ebx
        pop ebp
    }
    return itemResult;
}
static void ItemCase(bool available, bool hasProperty) {
    memset(frame, 0, sizeof(frame));
    auto fp = static_cast<BYTE*>(framePointer);
    *reinterpret_cast<int*>(fp + 8) = 2388023;
    *reinterpret_cast<Item**>(fp - 0x1c) = available ? &item : nullptr;
    *reinterpret_cast<Object**>(fp - 0x14) = hasProperty ? &property : nullptr;
    *reinterpret_cast<DWORD*>(fp - 0x10) = 0xdeadbeef;
    property.refs = hasProperty ? 1 : 0; property.releases = 0;
    item.refs = available ? 1 : 0;
    calculates = caches = itemReleases = 0;
    const unsigned csr = _mm_getcsr();
    disturbFloatingState = true;
    const DWORD result = RunItem();
    disturbFloatingState = false;
    Require(_mm_getcsr() == csr, "item helper preserves MXCSR");
    Require(result == (available ? 0x12345678u : 0), "CRC result");
    Require(calculates == int(available) && caches == int(available), "failed read never converted or cached");
    Require(!available || (cachedItem == 2388023 && cachedChecksum == result), "native cache key/value");
    Require(property.refs == 0 && property.releases == unsigned(hasProperty), "property released exactly once");
    Require(item.refs == 0 && itemReleases == int(available), "native ZRef lifetime preserved");
    Require(*reinterpret_cast<int*>(fp - 4) == -1, "native cleanup state restored");
}

static int loads = 0, copies = 0;
static bool loadSucceeds = true, throwOnCopy = false;
static void* expectedCanvas = nullptr;
static void* LoadCanvas() {
    ++loads;
    if (!loadSucceeds) return nullptr;
    fallbackCanvas.AddRef(); return &fallbackCanvas;
}
static HRESULT __fastcall Copy(void* self, void*, int x, int y, void* source, const VARIANT* alpha) {
    Require(self == reinterpret_cast<void*>(42) && x == 472 && y == 548 && alpha->vt == VT_I4,
        "copy ABI and arguments");
    Require(source && source == expectedCanvas, "never forward null canvas");
    ++copies;
    if (throwOnCopy) throw std::runtime_error("native failure");
    return S_OK;
}

static void CheckNativeBracket(const wchar_t* exePath) {
    // Exercise the production loader against the real WZ components and IMG,
    // in this isolated process. No game initialization, window or connection.
    std::wstring directory(exePath);
    directory.resize(directory.find_last_of(L"\\/"));
    Require(SetCurrentDirectoryW(directory.c_str()) != FALSE, "resource directory");
    Require(SUCCEEDED(CoInitialize(nullptr)), "COM initialization");
    HMODULE pcom = LoadLibraryW((directory + L"/PCOM.dll").c_str());
    Require(pcom != nullptr, "load PCOM");
    using Init = HRESULT(__cdecl*)();
    using Factory = HRESULT(__cdecl*)(const wchar_t*, const GUID*, void**, void*);
    using Root = HRESULT(__cdecl*)(void**, int);
    auto init = reinterpret_cast<Init>(GetProcAddress(pcom, "PcInitModule"));
    auto create = reinterpret_cast<Factory>(GetProcAddress(pcom, "PcCreateObject"));
    auto root = reinterpret_cast<Root>(GetProcAddress(pcom, "PcRootNameSpace"));
    Require(init && create && root && SUCCEEDED(init()), "resource factory");
    const GUID rmId = {0x57dfe40b,0x3e20,0x4dbc,{0x97,0xe8,0x80,0x5a,0x50,0xf3,0x81,0xbf}};
    const GUID nsId = {0x2aeeeb36,0xa4e1,0x4e2b,{0x8f,0x6f,0x2e,0x7b,0xde,0xc5,0xc5,0x3d}};
    const GUID fsId = {0x352d8655,0x51e4,0x4668,{0x8c,0xe4,0x08,0x66,0xe2,0xb6,0xa5,0xb5}};
    void *rm = nullptr, *ns = nullptr, *fs = nullptr;
    Require(SUCCEEDED(create(L"ResMan", &rmId, &rm, nullptr)) && rm, "create ResMan");
    Require(SUCCEEDED(create(L"NameSpace", &nsId, &ns, nullptr)) && ns, "create namespace");
    Require(SUCCEEDED(create(L"NameSpace#FileSystem", &fsId, &fs, nullptr)) && fs, "create filesystem");
    using SetParams = HRESULT(__stdcall*)(void*, int, int, int);
    using FsInit = HRESULT(__stdcall*)(void*, BSTR);
    using Mount = HRESULT(__stdcall*)(void*, BSTR, void*, int);
    using ResourceReadGuards::Method;
    Require(SUCCEEDED(Method<SetParams>(rm, 0x14)(rm, 0x11, -1, -1)), "resource flags");
    Require(SUCCEEDED(root(&ns, 1)), "resource root");
    BSTR path = SysAllocString((directory + L"/Data").c_str());
    Require(SUCCEEDED(Method<FsInit>(fs, 0x34)(fs, path)), "mount Data directory");
    SysFreeString(path); path = SysAllocString(L"/");
    Require(SUCCEEDED(Method<Mount>(ns, 0x18)(ns, path, fs, 0)), "mount namespace");
    SysFreeString(path);
    *reinterpret_cast<void**>(0x00BF14E8) = rm;
    void* loaded = ResourceReadGuards::LoadBracket();
    Require(loaded != nullptr, "production bracket loader reads current IMG");
    using Dimension = HRESULT(__stdcall*)(void*, int*);
    int width = 0, height = 0;
    Require(SUCCEEDED(Method<Dimension>(loaded, 0x40)(loaded, &width))
        && SUCCEEDED(Method<Dimension>(loaded, 0x48)(loaded, &height))
        && width > 0 && height > 0, "recovered canvas has valid dimensions");
    static_cast<IUnknown*>(loaded)->Release();
    *reinterpret_cast<void**>(0x00BF14E8) = nullptr;
    static_cast<IUnknown*>(rm)->Release();
    static_cast<IUnknown*>(fs)->Release();
    void* none = nullptr; root(&none, 1);
    static_cast<IUnknown*>(ns)->Release();
    using Term = void(__cdecl*)();
    auto terminate = reinterpret_cast<Term>(GetProcAddress(pcom, "PcTermModule"));
    Require(terminate != nullptr, "resource shutdown export");
    terminate();
    CoUninitialize();
}

int wmain(int argc, wchar_t** argv) {
    Require(argc == 2, "EXE argument"); MapClient(argv[1]);
    using namespace ResourceReadGuards;
    const BYTE avatarBytes[] = {0x8b,0x46,0x20,0x8b,0x40,0x14};
    const BYTE itemBytes[] = {0x8b,0x45,0xe4,0x51,0x51};
    const BYTE canvasBytes[] = {0xe8,0x97,0xa6,0xb4,0xff};
    Require(Patch(avatarSite, avatarBytes, sizeof(avatarBytes), AvatarSlots, 0xe9), "avatar signature/patch");
    Require(Patch(itemSite, itemBytes, sizeof(itemBytes), ItemInfo, 0xe9), "item signature/patch");
    Require(Patch(0x008DA50B, canvasBytes, sizeof(canvasBytes), CopyStatusBracket, 0xe8), "canvas signature/patch");
    Require(!Patch(avatarSite, avatarBytes, sizeof(avatarBytes), AvatarSlots, 0xe9), "reject already modified site");
    Require(*reinterpret_cast<WORD*>(0x00401D52) == 0x8b66, "native pair-loop target intact");
    Jump(0x004036AA, FindSlot); Jump(0x004036FA, SetSlot); Jump(0x00401DC7, ReturnFromSort);
    BSTR sr = SysAllocString(L"Sr"), multi = SysAllocString(L"MaPn"), empty = SysAllocString(L""), null = nullptr;
    AvatarCase(&multi, &sr, 2, false);
    Require(slotKeys[0] == ((DWORD(L'M') << 16) | L'a')
        && slotKeys[1] == ((DWORD(L'P') << 16) | L'n'), "native two-pair ordering");
    AvatarCase(nullptr, &sr, 1, false);
    AvatarCase(&null, &multi, 2, false);
    AvatarCase(&empty, &sr, 0, false);
    AvatarCase(nullptr, nullptr, 0, true);
    AvatarCase(&null, &null, 0, true);
    const int before = reports;
    for (int i = 0; i < 30; ++i) AvatarCase(nullptr, &sr, 1, false);
    Require(reports - before <= 8, "avatar diagnostics bounded");
    SysFreeString(sr); SysFreeString(multi); SysFreeString(empty);

    Jump(0x005DC28A, AddItemRef); Jump(0x005DADF5, Calculate); Jump(0x005DC94C, ReleaseItemRef);
    Jump(0x005DD35B, Cache); Jump(0x005DADE1, ReturnChecksum);
    ItemCase(false, false); ItemCase(false, true); ItemCase(true, true);

    copyCanvas = reinterpret_cast<CopyCanvas>(Copy); loadBracket = LoadCanvas;
    VARIANT alpha{}; alpha.vt = VT_I4; alpha.lVal = 255;
    expectedCanvas = &nativeCanvas;
    Require(CopyStatusBracket(reinterpret_cast<void*>(42), nullptr, 472, 548, &nativeCanvas, &alpha) == S_OK,
        "normal status draw");
    Require(loads == 0, "normal draw has no resource query");
    expectedCanvas = &fallbackCanvas;
    for (int i = 0; i < 20; ++i)
        Require(CopyStatusBracket(reinterpret_cast<void*>(42), nullptr, 472, 548, nullptr, &alpha) == S_OK,
            "recovered bracket draw");
    Require(loads == 1 && fallbackCanvas.refs == 1, "one owned fallback avoids frame-by-frame reloads");
    expectedCanvas = &nativeCanvas;
    CopyStatusBracket(reinterpret_cast<void*>(42), nullptr, 472, 548, &nativeCanvas, &alpha);
    Require(fallbackCanvas.refs == 0, "fallback released on native recovery");
    loadSucceeds = false; const int oldCopies = copies;
    for (int i = 0; i < 20; ++i)
        Require(CopyStatusBracket(reinterpret_cast<void*>(42), nullptr, 472, 548, nullptr, &alpha) == S_FALSE,
            "unavailable bracket skips only this copy");
    Require(loads == 2 && copies == oldCopies, "failed retry is throttled");
    loadSucceeds = true; lastAttempt = GetTickCount() - 1001; expectedCanvas = &fallbackCanvas;
    Require(CopyStatusBracket(reinterpret_cast<void*>(42), nullptr, 472, 548, nullptr, &alpha) == S_OK,
        "transient missing canvas recovers after retry interval");
    expectedCanvas = &nativeCanvas; throwOnCopy = true;
    bool propagated = false;
    try { CopyStatusBracket(reinterpret_cast<void*>(42), nullptr, 472, 548, &nativeCanvas, &alpha); }
    catch (const std::runtime_error&) { propagated = true; }
    Require(propagated && fallbackCanvas.refs == 0, "unrelated native errors are not swallowed");
    CheckNativeBracket(argv[1]);
    std::puts("PASS: native slot loop, CRC cleanup/cache, x86 stack ABI, floating state, canvas recovery/refcounts and bounded retries");
    return 0;
}
