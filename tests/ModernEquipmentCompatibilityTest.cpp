#include <windows.h>
#include <oleauto.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <stdexcept>
#include <cstdarg>
#include <psapi.h>
#include <xmmintrin.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "psapi.lib")
static void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static int renderEvents = 0;
static char renderEvent[512]{};
namespace CrashReporter { void RecordEvent(const char* category, const char* format, ...) {
    if (std::strcmp(category, "equipment.pet-render")) return;
    ++renderEvents;
    va_list args; va_start(args, format);
    vsnprintf_s(renderEvent, sizeof(renderEvent), _TRUNCATE, format, args);
    va_end(args);
} }
static int protectionCalls = 0, deniedProtectionCall = 0;
static BOOL WINAPI TestVirtualProtect(void* address, SIZE_T size, DWORD protection, DWORD* old) {
    if (++protectionCalls == deniedProtectionCall) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return VirtualProtect(address, size, protection, old);
}
#define VirtualProtect TestVirtualProtect
#include "ModernEquipmentCompatibilityUnderTest.h"
#include "NameTagOriginFixUnderTest.h"
#undef VirtualProtect
#include "NameTagOriginFixCases.h"
bool Memory::SetHook(bool, void**, void*) { return false; }
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
    // The client's entry point is never called. This mapping checks the
    // supported call-site signatures; resource decoding uses PCOM separately.
}

static int iconCalls = 0, iconMode = 0;
static void CheckNativePetLife() {
    // Run the actual v83 pet dried-up predicate, relocating only its constant
    // pointer and CompareFileTime import in this isolated, uninitialized copy.
    Require(*reinterpret_cast<DWORD*>(0x004E4045) == 0xAF30B0
        && *reinterpret_cast<DWORD*>(0x004E404F) == 0xBF03B4,
        "native pet life predicate operands");
    *reinterpret_cast<DWORD*>(0x004E4045) = 0x00AF30B0;
    *reinterpret_cast<DWORD*>(0x004E404F) = 0x00BF03B4;
    *reinterpret_cast<void**>(0x00BF03B4) = reinterpret_cast<void*>(&CompareFileTime);
    using Dried = int(__thiscall*)(void*);
    auto dried = reinterpret_cast<Dried>(0x004E4044);
    BYTE pet[0x80]{};
    ULONGLONG expiration = 150842304000000000ULL;
    memcpy(pet + 0x59, &expiration, sizeof(expiration));
    Require(dried(pet) == 1, "server expiration=-1 is dried in v83");
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    memcpy(&expiration, &now, sizeof(expiration));
    expiration += 30ULL * 24 * 60 * 60 * 10000000;
    memcpy(pet + 0x59, &expiration, sizeof(expiration));
    Require(dried(pet) == 0, "30-day pet lifetime is alive in v83");
    std::puts("PASS: native v83 pet predicate rejects expiration=-1 and accepts 30-day life");
}
static void* iconThis = reinterpret_cast<void*>(42);
struct IconObject : IUnknown {
    ULONG refs = 0;
    HRESULT queryResult = S_OK;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override {
        *out = nullptr;
        if (FAILED(queryResult)) return queryResult;
        *out = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { Require(refs > 0, "no COM underflow"); return --refs; }
} iconObject;
static void* iconSentinel = &iconObject;
static void** __fastcall IconFixture(void* self, void*, void** out, int id, int useIcon, int disabled) {
    Require(self == iconThis && id == 1703494, "icon thiscall and item ID");
    Require(disabled == (iconMode == 3 ? 1 : 0), "disabled selector retained");
    ++iconCalls;
    if (iconMode == 4) throw std::runtime_error("original failure");
    *out = iconMode == 0 || (iconMode == 1 && !useIcon) ? iconSentinel : nullptr;
    if (*out) iconObject.AddRef();
    return out;
}
static void CheckIconContract() {
    using namespace ModernEquipmentCompatibility;
    getIcon = reinterpret_cast<GetIcon>(IconFixture);
    for (iconMode = 0; iconMode < 4; ++iconMode) {
        iconCalls = 0;
        void* canvas = nullptr;
        Require(ItemIcon(iconThis, nullptr, &canvas, 1703494, 1, iconMode == 3) == &canvas,
            "out pointer returned");
        Require(iconCalls == (iconMode == 0 ? 1 : 2), "valid icon untouched; fallback once");
        Require(canvas == (iconMode < 2 ? iconSentinel : nullptr), "missing stays missing");
        Require(iconObject.refs == (canvas ? 1u : 0u), "exactly one owning result reference");
        if (canvas) static_cast<IUnknown*>(canvas)->Release();
        Require(iconObject.refs == 0, "caller releases complete icon lifetime");
    }
    iconMode = 2; iconCalls = 0;
    void* canvas = nullptr;
    ItemIcon(iconThis, nullptr, &canvas, 1703494, 0, 0);
    Require(iconCalls == 1, "raw lookup never recurses");
    iconMode = 4; iconCalls = 0;
    bool threw = false;
    try { ItemIcon(iconThis, nullptr, &canvas, 1703494, 1, 0); }
    catch (const std::runtime_error&) { threw = true; }
    Require(threw && iconCalls == 1, "original exceptions propagate");
}

static void** __fastcall NativeIcon(void*, void*, void** out, int id, int useIcon, int disabled) {
    wchar_t path[128];
    swprintf_s(path, L"Character/%s/%08d.img/info/%s%s",
        id / 10000 == 170 ? L"Weapon" : L"Cape", id,
        useIcon ? L"icon" : L"iconRaw", disabled ? L"D" : L"");
    *out = ModernEquipmentCompatibility::LoadCanvas(path);
    return out;
}

static BYTE faceFrame[0x200];
static void* faceFramePointer = faceFrame + 0x180;
static void* sourceCanvas;
static DWORD faceCall = reinterpret_cast<DWORD>(ModernEquipmentCompatibility::AssignFaceCanvas);
static HRESULT assignmentResult;
static void CheckPixels(void* canvas, int width, int height) {
    using Pixel = HRESULT(__stdcall*)(void*, int, int, unsigned*);
    int opaque = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        unsigned pixel = 0;
        Require(SUCCEEDED(ModernEquipmentCompatibility::Method<Pixel>(canvas, 0x88)(canvas, x, y, &pixel)),
            "native decoder reads every pixel");
        opaque += (pixel >> 24) != 0;
    }
    Require(opaque > 0, "canvas has visible pixels");
}
static void RunFaceAssignment() {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp, faceFramePointer
        lea ecx, [ebp - 14h]
        lea eax, sourceCanvas
        push eax
        call dword ptr [faceCall]
        mov assignmentResult, eax
        pop edi
        pop esi
        pop ebx
        pop ebp
    }
}
static void FaceCase(int id, const wchar_t* path, int expectedWidth, int expectedHeight) {
    using namespace ModernEquipmentCompatibility;
    sourceCanvas = LoadCanvas(path);
    Require(sourceCanvas != nullptr, "native face canvas resolves including UOL");
    auto frame = static_cast<BYTE*>(faceFramePointer);
    *reinterpret_cast<int*>(frame - 0x138) = 20000;
    *reinterpret_cast<int*>(frame - 0x130) = id;
    const unsigned csr = _mm_getcsr();
    RunFaceAssignment();
    Require(_mm_getcsr() == csr, "bridge preserves floating-point state");
    Require(assignmentResult == S_OK, "assignment HRESULT preserved");
    void* result = *reinterpret_cast<void**>(frame - 0x14);
    int width = 0, height = 0;
    Require(Dimensions(result, width, height) && width == expectedWidth && height == expectedHeight,
        "native face canvas dimensions after compatibility");
    CheckPixels(result, width, height);
    if (id != 1012634) Require(result == sourceCanvas, "real expression artwork preserved");
    static_cast<IUnknown*>(sourceCanvas)->Release();
    static_cast<IUnknown*>(result)->Release();
    *reinterpret_cast<void**>(frame - 0x14) = nullptr;
}
static void CheckNativeAssignmentContract() {
    // Execute the EXE's actual QueryInterface/Release assignment routine.
    // Only its absolute IID literal needs relocation in the isolated mapping.
    Require(*reinterpret_cast<DWORD*>(0x0041E442) == 0xBD82F8, "native assignment IID operand");
    *reinterpret_cast<DWORD*>(0x0041E442) = 0x00BD82F8;
    auto output = reinterpret_cast<void**>(static_cast<BYTE*>(faceFramePointer) - 0x14);
    for (HRESULT expected : {S_OK, E_NOINTERFACE, E_FAIL}) {
        IconObject old;
        old.AddRef(); *output = &old;
        iconObject.queryResult = expected;
        iconObject.AddRef(); sourceCanvas = &iconObject;
        RunFaceAssignment();
        Require(assignmentResult == expected, "native HRESULT preserved including failures");
        Require(old.refs == 0, "previous canvas released once by native routine");
        if (SUCCEEDED(expected)) {
            Require(*output == &iconObject && iconObject.refs == 2, "native QI owns result");
            static_cast<IUnknown*>(*output)->Release();
        } else Require(!*output && iconObject.refs == 1, "failed native QI clears destination");
        iconObject.Release();
        *output = nullptr;
    }
    iconObject.queryResult = S_OK;
    IconObject old; old.AddRef(); *output = &old;
    sourceCanvas = nullptr;
    RunFaceAssignment();
    Require(assignmentResult == E_NOINTERFACE && !*output && old.refs == 0,
        "missing native input remains E_NOINTERFACE with correct cleanup");
}
static SIZE_T PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    Require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
        sizeof(memory)) != FALSE, "test process memory measurement");
    return memory.PrivateUsage;
}
static int nativePetMatchCalls = 0;
static int __fastcall LegacyPetMatch(void*, void*, int) { ++nativePetMatchCalls; return 77; }
static void CheckPetEquipmentResources() {
    using namespace ModernEquipmentCompatibility;
    // Real TSecType<int> decoding: word1=0, word0=id, checksum=rol(id^magic,5).
    auto checksum = reinterpret_cast<int(__cdecl*)(int,int)>(0x00A61014);
    BYTE equipment[0x200]{};
    auto setId = [&](int id) {
        std::memcpy(equipment, &id, 4);
        int check = checksum(id ^ 0xBAADF00D, 5);
        std::memcpy(equipment + 8, &check, 4);
        Require(readSecureId(equipment, check) == id, "native secure equipment ID");
    };
    petMatch = reinterpret_cast<PetMatch>(LegacyPetMatch);
    const int outfits[] = {1802896,1802897,1802899};
    for (int i = 0; i < 3; ++i) {
        setId(outfits[i]);
        for (int base : {5002356,5002365,5002414}) {
            for (int j = 0; j < 3; ++j) {
                int petId = base + j;
                SetLastError(1234);
                Require(PetEquipment(equipment, nullptr, petId) == (i == j), "full-ID pet outfit pairing");
                Require(GetLastError() == 1234, "pet match preserves last error");
                // 5002365..67 use action-level UOLs; direct ResMan paths do
                // not traverse these (covered by the canonical canvas below).
                if (i == j && base != 5002365) {
                    wchar_t path[128];
                    swprintf_s(path, L"Character/PetEquip/%08d.img/%d/stand0/0", outfits[i], petId);
                    void* canvas = LoadCanvas(path);
                    int width = 0, height = 0;
                    Require(Dimensions(canvas, width, height), "matching pet outfit animation exists");
                    CheckPixels(canvas, width, height);
                    static_cast<IUnknown*>(canvas)->Release();
                }
            }
        }
        for (int wrong : {5000000,5000414,5003414,4999999,5010000,0,-1})
            Require(PetEquipment(equipment, nullptr, wrong) == 0, "wrong or colliding pet ID rejected");
    }
    setId(1809999);
    Require(PetEquipment(equipment, nullptr, 5002414) == 0, "missing outfit rejected");
    Require(nativePetMatchCalls == 0, "modern pet IDs never enter native 128-bit mask");
    setId(1802000);
    Require(MatchPetResource(1802000, 5000000) == 1, "existing legacy outfit resolves by full ID");
    Require(PetEquipment(equipment, nullptr, 5000000) == 77, "legacy outfit preserves native policy");
    setId(1812000);
    Require(PetEquipment(equipment, nullptr, 5002414) == 77, "non-outfit pet items preserve native policy");
    Require(nativePetMatchCalls == 2, "only unchanged categories use native predicate");
    std::puts("PASS: native secure IDs, 27 full-ID pet pairs, 6 real outfit canvases, negative and legacy cases");
}
static void CheckPetMaskInstallation() {
    using namespace ModernEquipmentCompatibility;
    DWORD ignored = 0;
    auto site = reinterpret_cast<BYTE*>(0x005CD155);
    Require(VirtualProtect(site, sizeof(petMaskOriginal), PAGE_EXECUTE_READ, &ignored), "pet mask page setup");
    protectionCalls = 0; deniedProtectionCall = 1;
    Require(!WritePetMaskBound(true), "pet mask protection failure stops patch");
    Require(!std::memcmp(site, petMaskOriginal, sizeof(petMaskOriginal)), "failed patch unchanged");
    deniedProtectionCall = 0;
    Require(!InstallPetEquipment(), "fixture detour failure reported");
    Require(!std::memcmp(site, petMaskOriginal, sizeof(petMaskOriginal)), "detour failure rolls back mask patch");
    Require(WritePetMaskBound(true), "pet mask signature patch");
    Require(!WritePetMaskBound(true), "repeat mask patch rejected");
    // Execute the patched comparison in isolation: the rest of the loader is
    // unchanged. This checks instruction lengths and the conditional branch.
    BYTE code[27] = {0x8b,0x44,0x24,0x04}; // eax = first argument
    std::memcpy(code + 4, site, 14);
    int skip = 6; std::memcpy(code + 14, &skip, 4);
    const BYTE ends[] = {0xb8,1,0,0,0,0xc3,0x31,0xc0,0xc3};
    std::memcpy(code + 18, ends, sizeof(ends));
    void* executable = VirtualAlloc(nullptr, sizeof(code), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    Require(executable != nullptr, "mask comparison test allocation");
    std::memcpy(executable, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), executable, sizeof(code));
    auto buildMask = reinterpret_cast<int(__cdecl*)(int)>(executable);
    for (int id = 1800000; id < 1810000; ++id)
        Require(buildMask(id) == (id >= 1802000 && id < 1802100), "only original outfit range enters 128-bit mask builder");
    VirtualFree(executable, 0, MEM_RELEASE);
    Require(WritePetMaskBound(false), "mask patch restore");
    MEMORY_BASIC_INFORMATION page{};
    Require(VirtualQuery(site, &page, sizeof(page)) && page.Protect == PAGE_EXECUTE_READ, "mask page protection restored");
}
static int renderCalls = 0, renderMode = 0;
static DWORD renderNode[2]{}, renderFrame[4]{};
static void __fastcall PetActionFixture(void* self, void*, void* pet, int action, int equipmentId, void* output) {
    Require(self == iconThis && *static_cast<int*>(pet) == 5002416 && action == 1
        && equipmentId == 1802899, "pet renderer preserves arguments");
    ++renderCalls;
    if (renderMode == 2) throw std::runtime_error("native render failure");
    auto list = static_cast<DWORD*>(output);
    list[2] = 12; list[3] = reinterpret_cast<DWORD>(renderNode);
    SetLastError(2468);
}
static void CheckPetRenderingTrace() {
    using namespace ModernEquipmentCompatibility;
    const BYTE expected[] = {0xb8,0x5f,0x6c,0xa7,0x00,0xe8,0xe9,0x29,0x65,0x00};
    Require(!std::memcmp(reinterpret_cast<void*>(loadPetAction), expected, sizeof(expected)), "pet renderer signature");
    Require(!InstallPetRenderingTrace(), "fixture detour failure reported without patch");
    loadPetAction = reinterpret_cast<LoadPetAction>(PetActionFixture);
    DWORD output[5]{}; int pet = 5002416;
    renderNode[1] = reinterpret_cast<DWORD>(renderFrame);
    for (renderMode = 0; renderMode < 2; ++renderMode) {
        auto canvas = LoadCanvas(renderMode == 0 ? L"Item/Pet/5002416.img/stand0/0"
            : L"Character/PetEquip/01802899.img/5002416/stand0/0");
        Require(canvas != nullptr, "pet render trace actual canvas fixture");
        renderFrame[3] = reinterpret_cast<DWORD>(canvas);
        PetAction(iconThis, nullptr, &pet, 1, 1802899, output);
        Require(GetLastError() == 2468 && output[2] == 12, "trace preserves native result and error");
        Require(strstr(renderEvent, "frames=12 canvas=1") && strstr(renderEvent, renderMode ? "sameBase=0" : "sameBase=1"), "trace distinguishes base fallback from separate canvas");
        static_cast<IUnknown*>(canvas)->Release();
    }
    renderMode = 2; bool threw = false;
    try { PetAction(iconThis, nullptr, &pet, 1, 1802899, output); }
    catch (const std::runtime_error&) { threw = true; }
    Require(threw, "native compositor exception propagates");
    renderMode = 0; petRenderReports = 24; int events = renderEvents;
    for (int i = 0; i < 100; ++i) PetAction(iconThis, nullptr, &pet, 1, 1802899, output);
    Require(renderCalls == 103 && renderEvents == events, "trace stops at cap while native calls continue");
    std::puts("PASS: pet rendering trace arguments, COM identities, native result/error, exception and event cap");
}
static void CheckInstalledResources() {
    using namespace ModernEquipmentCompatibility;
    getIcon = reinterpret_cast<GetIcon>(NativeIcon);
    const int ids[] = {1702845,1702796,1703448,1703264,1703494,1703362,1702565,1703244,1103568,1703193};
    for (int id : ids) {
        void* canvas = nullptr;
        ItemIcon(nullptr, nullptr, &canvas, id, 1, 0);
        int width = 0, height = 0;
        Require(Dimensions(canvas, width, height) && width > 1 && height > 1, "reported item icon native load");
        CheckPixels(canvas, width, height);
        std::printf("native icon %d: %dx%d\n", id, width, height);
        static_cast<IUnknown*>(canvas)->Release();
    }
    FaceCase(1012634, L"Character/Accessory/01012634.img/default/default", 26, 8);
    FaceCase(1012634, L"Character/Accessory/01012634.img/blink/0/default", 26, 8);
    FaceCase(1012823, L"Character/Accessory/01012823.img/default/default", 24, 8);
    FaceCase(1012634, L"Character/Accessory/01012634.img/default/default", 26, 8);
    FaceCase(1012823, L"Character/Accessory/01012823.img/blink/0/default", 24, 8);
    for (int frame = 0; frame < 16; ++frame) {
        wchar_t path[128];
        if (frame) swprintf_s(path, L"Character/Cape/01103568.img/default/default%d", frame);
        else wcscpy_s(path, L"Character/Cape/01103568.img/default/default");
        void* canvas = LoadCanvas(path);
        int width = 0, height = 0;
        Require(Dimensions(canvas, width, height), "cape effect frame resolves");
        CheckPixels(canvas, width, height);
        static_cast<IUnknown*>(canvas)->Release();
    }
    for (int i = 0; i < 100; ++i)
        FaceCase(1012634, L"Character/Accessory/01012634.img/default/default", 26, 8);
    const SIZE_T before = PrivateBytes();
    for (int i = 0; i < 2000; ++i)
        FaceCase(1012634, L"Character/Accessory/01012634.img/default/default", 26, 8);
    std::printf("2000 native face load/release cycles: private bytes before=%zu after=%zu\n", before, PrivateBytes());
}
static void CheckFollowupResources() {
    using namespace ModernEquipmentCompatibility;
    auto pixels = [](const wchar_t* path) {
        void* canvas = LoadCanvas(path);
        int width = 0, height = 0;
        if (!Dimensions(canvas, width, height)) {
            std::fwprintf(stderr, L"unresolved: %s\n", path);
            Require(false, "followup canvas resolves");
        }
        CheckPixels(canvas, width, height);
        static_cast<IUnknown*>(canvas)->Release();
    };
    for (int id : {1702116, 1702117}) {
        // Direct ResMan paths cannot traverse action-level UOLs here (the
        // unmodified 31/stand1 control fails too). Check the canonical action;
        // validate all weapon-type UOL targets separately with MapleLib.
        for (int type : {30}) {
            wchar_t path[128];
            swprintf_s(path, L"Character/Weapon/%08d.img/%d/sit/0/weapon", id, type);
            pixels(path);
        }
    }
    for (int id : {605,606,607}) {
        for (const wchar_t* piece : {L"w",L"c",L"e"}) {
            wchar_t path[128];
            swprintf_s(path, L"UI/NameTag.img/pet/%d/%s", id, piece);
            pixels(path);
        }
        for (const wchar_t* piece : {L"nw",L"n",L"ne",L"sw",L"s",L"se",L"w",L"c",L"e",L"arrow"}) {
            wchar_t path[128];
            swprintf_s(path, L"UI/ChatBalloon.img/pet/%d/%s", id, piece);
            pixels(path);
        }
    }
    std::puts("PASS: 2 canonical Chinese knot sitting canvases and 39 pet UI canvases, native pixels");
}
static BYTE petActionFrame[0x100]{};
static void* petActionFramePointer = petActionFrame + 0x80;
static void* petActionSource = nullptr;
static void* petActionOutput = nullptr;
static DWORD petActionBridge = reinterpret_cast<DWORD>(ModernEquipmentCompatibility::AssignPetActionProperty);
static HRESULT petActionResult;
static void RunPetActionAssignment() {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp, petActionFramePointer
        lea ecx, petActionOutput
        lea eax, petActionSource
        push eax
        call dword ptr [petActionBridge]
        mov petActionResult, eax
        pop edi
        pop esi
        pop ebx
        pop ebp
    }
}
static VARIANT ReadPetProperty(void* property, const wchar_t* name) {
    VARIANT value{};
    BSTR key = SysAllocString(name);
    Require(key != nullptr, "pet property key allocation");
    const HRESULT hr = ModernEquipmentCompatibility::Method<HRESULT(__stdcall*)(void*, BSTR, VARIANT*)>(property, 0x14)(property, key, &value);
    SysFreeString(key);
    Require(SUCCEEDED(hr), "native pet property read");
    return value;
}
static void CheckPetActionUols() {
    using namespace ModernEquipmentCompatibility;
    // Execute the shipped EXE's actual QI/Release routine and the new x86
    // bridge. Only its IID absolute address needs relocation in this mapping.
    Require(*reinterpret_cast<DWORD*>(0x004052C4) == 0xBD8308, "native property assignment IID operand");
    *reinterpret_cast<DWORD*>(0x004052C4) = 0x00BD8308;
    const BYTE expected[] = {0xe8,0xab,0x6f,0xff,0xff};
    const auto site = reinterpret_cast<BYTE*>(0x0040E2FD);
    Require(!std::memcmp(site, expected, sizeof(expected)), "pet action assignment call signature");
    DWORD ignored = 0;
    Require(VirtualProtect(site, sizeof(expected), PAGE_EXECUTE_READ, &ignored), "pet action patch page setup");
    protectionCalls = 0; deniedProtectionCall = 1;
    Require(!InstallPetActionUols() && !std::memcmp(site, expected, sizeof(expected)), "denied pet action patch changes nothing");
    deniedProtectionCall = 0;
    Require(InstallPetActionUols() && !InstallPetActionUols(), "pet action patch installs once");
    Require(0x0040E302 + *reinterpret_cast<DWORD*>(site + 1) == petActionBridge, "pet action call reaches tested bridge");
    MEMORY_BASIC_INFORMATION page{};
    Require(VirtualQuery(site, &page, sizeof(page)) && page.Protect == PAGE_EXECUTE_READ, "pet action page protection restored");

    static const GUID propertyId = {0x986515d9,0x0a0b,0x4929,{0x8b,0x4f,0x71,0x86,0x82,0x17,0x7b,0x92}};
    for (int petId : {5000042, 5002414, 5002415, 5002416}) {
        wchar_t path[96]; swprintf_s(path, L"Item/Pet/%d.img", petId);
        void* root = LoadInterface(path, propertyId);
        Require(root != nullptr, "native pet IMG root");
        for (const wchar_t* action : {L"jump", L"fly"}) {
            VARIANT raw = ReadPetProperty(root, action);
            Require(raw.vt == VT_UNKNOWN && raw.punkVal, "native action object");
            petActionSource = raw.punkVal;
            const bool alias = petId != 5000042 && !wcscmp(action, L"fly");
            const HRESULT before = assignPetProperty(&petActionOutput, &petActionSource);
            Require(before == (alias ? E_NOINTERFACE : S_OK), "reproduce original action UOL rejection");
            if (petActionOutput) static_cast<IUnknown*>(petActionOutput)->Release();
            petActionOutput = nullptr;
            // Match the native caller's BSTR Data_t layout, not a wchar_t*
            // accidentally interpreted as the wrapper's shared string data.
            BSTR actionBstr = SysAllocString(action);
            void* actionData[3] = {actionBstr, nullptr, reinterpret_cast<void*>(1)};
            *reinterpret_cast<void**>(static_cast<BYTE*>(petActionFramePointer) - 0x38) = actionData;
            *reinterpret_cast<int*>(static_cast<BYTE*>(petActionFramePointer) - 0x2c) = petId;
            for (int repeat = 0; repeat < 32; ++repeat) {
                SetLastError(9876);
                RunPetActionAssignment();
                Require(petActionResult == S_OK && petActionOutput, "native bridge resolves real pet action including UOL");
                Require(GetLastError() == 9876, "pet action resolution preserves last error");
                VARIANT frame = ReadPetProperty(petActionOutput, L"0");
                Require(frame.vt == VT_UNKNOWN && frame.punkVal, "resolved action has frame zero");
                void* canvas = nullptr;
                static const GUID canvasId = {0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
                Require(SUCCEEDED(frame.punkVal->QueryInterface(canvasId, &canvas)), "resolved frame is a native canvas");
                int width = 0, height = 0;
                Require(Dimensions(canvas, width, height), "resolved frame dimensions");
                if (repeat == 0) CheckPixels(canvas, width, height);
                static_cast<IUnknown*>(canvas)->Release(); VariantClear(&frame);
                // Deliberately keep output alive across assignments: the next
                // native call must release it and acquire exactly one result.
            }
            static_cast<IUnknown*>(petActionOutput)->Release(); petActionOutput = nullptr;
            if (alias) {
                void*& rm = *reinterpret_cast<void**>(0x00BF14E8);
                void* saved = rm; rm = nullptr;
                Require(ResolvePetActionProperty(&petActionOutput, &petActionSource, petId, action) == E_NOINTERFACE
                    && !petActionOutput, "unavailable resolver preserves original failure");
                rm = saved;
                Require(ResolvePetActionProperty(&petActionOutput, &petActionSource, 4000000, action) == E_NOINTERFACE
                    && !petActionOutput, "non-pet category cannot activate resolution");
                Require(ResolvePetActionProperty(&petActionOutput, &petActionSource, petId, L"missingAction") == E_NOINTERFACE
                    && !petActionOutput, "missing action stays missing rather than inventing frames");
            } else {
                Require(petActionSource != nullptr, "ordinary action source kept alive");
            }
            SysFreeString(actionBstr); VariantClear(&raw); petActionSource = nullptr;
        }
        static_cast<IUnknown*>(root)->Release();
    }
    Require(ResolvePetActionProperty(&petActionOutput, &petActionSource, 5002416, L"fly") == E_NOINTERFACE
        && !petActionOutput, "null native action remains absent");
    std::puts("PASS: native pet action UOL failure reproduced; x86 bridge resolves 3 modern pets; legacy, pixels, repeat assignment and failure paths");
}
static void CheckNativeResources(const wchar_t* exePath, const wchar_t* followupRoot) {
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
    using ModernEquipmentCompatibility::Method;
    Require(SUCCEEDED(Method<SetParams>(rm, 0x14)(rm, 0x11, -1, -1)), "resource flags");
    Require(SUCCEEDED(root(&ns, 1)), "resource root");
    BSTR path = SysAllocString(followupRoot ? followupRoot : (directory + L"/Data").c_str());
    Require(SUCCEEDED(Method<FsInit>(fs, 0x34)(fs, path)), "mount Data directory");
    SysFreeString(path); path = SysAllocString(L"/");
    Require(SUCCEEDED(Method<Mount>(ns, 0x18)(ns, path, fs, 0)), "mount namespace");
    SysFreeString(path);
    *reinterpret_cast<void**>(0x00BF14E8) = rm;
    if (followupRoot) CheckFollowupResources();
    else { CheckInstalledResources(); CheckPetEquipmentResources(); CheckPetRenderingTrace(); CheckPetActionUols(); CheckNameTagResources(create); }
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
    setvbuf(stdout, nullptr, _IONBF, 0);
    Require(argc == 2 || argc == 3, "client path and optional staged resource root");
    MapClient(argv[1]);
    CheckNativePetLife();
    CheckPetMaskInstallation();
    CheckIconContract();
    using namespace ModernEquipmentCompatibility;
    DWORD ignored = 0;
    Require(VirtualProtect(reinterpret_cast<void*>(0x004081B2), 5, PAGE_EXECUTE_READ, &ignored)
        && VirtualProtect(reinterpret_cast<void*>(0x00409119), 5, PAGE_EXECUTE_READ, &ignored),
        "test patch pages begin executable/read-only");
    const BYTE expectedA[] = {0xe8,0x74,0x62,0x01,0x00};
    const BYTE expectedB[] = {0xe8,0x0d,0x53,0x01,0x00};
    for (int fail : {1, 2}) {
        protectionCalls = 0; deniedProtectionCall = fail;
        Require(!InstallFaceImages(), "denied page protection rejects installation");
        Require(!std::memcmp(reinterpret_cast<void*>(0x004081B2), expectedA, 5)
            && !std::memcmp(reinterpret_cast<void*>(0x00409119), expectedB, 5),
            "denied protection leaves both calls unchanged");
        for (DWORD site : {0x004081B2, 0x00409119}) {
            MEMORY_BASIC_INFORMATION page{};
            Require(VirtualQuery(reinterpret_cast<void*>(site), &page, sizeof(page)) != 0
                && page.Protect == PAGE_EXECUTE_READ, "page protection restored after failure");
        }
    }
    deniedProtectionCall = 0;
    Require(InstallFaceImages(), "both native face call signatures and patches");
    Require(!InstallFaceImages(), "already patched sites rejected");
    for (DWORD site : {0x004081B2, 0x00409119})
        Require(site + 5 + *reinterpret_cast<DWORD*>(site + 1) == faceCall,
            "native calls target the tested bridge");
    CheckNativeAssignmentContract();
    CheckNativeResources(argv[1], argc == 3 ? argv[2] : nullptr);
    std::puts("PASS: icon contract, native QI ownership/failures, real IMG pixels, face bridge, repeated load/release");
}
