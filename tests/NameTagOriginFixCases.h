#pragma once
#include <vector>
#include <algorithm>

// Execute the real native Copy instruction, without initializing the game.
// Only this isolated mapping's continuation is replaced with RET. EBP carries
// the original compositor's max-origin local; PCOM provides the actual pixels.
static DWORD nameTagCallSite = 0x005F0D36;
__declspec(naked) static HRESULT InvokeNameTagRight(void*, int, int, void*) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        sub esp, 60h
        mov esi, ebp
        lea ebp, [esp + 20h]
        mov eax, [esi + 10h]
        mov [ebp + 20h], eax
        push offset ResumeCopy
        push 0
        push 255
        push 0
        push 3
        push [esi + 14h]
        push 0
        push [esi + 0Ch]
        push [esi + 8]
        mov edi, [esi + 8]
        mov eax, [edi]
        jmp dword ptr [nameTagCallSite]
    ResumeCopy:
        mov ebp, esi
        add esp, 60h
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

static void CheckNameTagInstallation() {
    using namespace NameTagOriginFix;
    auto site = reinterpret_cast<BYTE*>(copySite);
    DWORD ignored = 0;
    Require(VirtualProtect(site, 6, PAGE_EXECUTE_READ, &ignored) != FALSE, "name-tag read-only page");
    for (int failure : {1, 2}) {
        protectionCalls = 0; deniedProtectionCall = failure;
        Require(!InstallPatch(), "name-tag protection failure rejects patch");
        Require(!std::memcmp(site, originalCall, 6), "name-tag failure rolls back native bytes");
        MEMORY_BASIC_INFORMATION page{};
        Require(VirtualQuery(site, &page, sizeof(page)) && page.Protect == PAGE_EXECUTE_READ,
            "name-tag failure restores executable/read-only protection");
    }
    deniedProtectionCall = 0;
    Require(InstallPatch(), "name-tag exact native instruction signatures");
    Require(!InstallPatch(), "name-tag duplicate install rejected");
    Require(site[0] == 0xe8 && site[5] == 0x90
        && copySite + 5 + *reinterpret_cast<DWORD*>(site + 1) == reinterpret_cast<DWORD>(CopyRight),
        "native right Copy redirects to production bridge");
}

static void* nameTagExpectedDestination;
static void* nameTagExpectedSource;
static HRESULT nameTagExpectedResult;
static HRESULT __stdcall CaptureNameTagCopy(void* self, int x, int y, void* source, VARIANT alpha) {
    Require(self == nameTagExpectedDestination && source == nameTagExpectedSource,
        "name-tag bridge retains source/destination pointers");
    Require(x == 123 && y == 4 && alpha.vt == VT_I4 && alpha.lVal == 255,
        "name-tag bridge changes only Y, retains X and complete alpha VARIANT");
    return nameTagExpectedResult;
}
static HRESULT __stdcall NameTagMockOrigin(void*, int* y) { *y = 2; return S_OK; }
static void CheckNameTagCopyContract() {
    void* destinationMethods[33]{};
    void* sourceMethods[30]{};
    destinationMethods[0x80 / 4] = reinterpret_cast<void*>(CaptureNameTagCopy);
    sourceMethods[0x74 / 4] = reinterpret_cast<void*>(NameTagMockOrigin);
    void** destination = destinationMethods;
    void** source = sourceMethods;
    nameTagExpectedDestination = &destination;
    nameTagExpectedSource = &source;
    for (HRESULT result : {S_OK, E_FAIL, E_INVALIDARG}) {
        nameTagExpectedResult = result;
        Require(InvokeNameTagRight(&destination, 123, 6, &source) == result,
            "name-tag native Copy HRESULT and stack balance preserved");
    }
}

using NameTagFactory = HRESULT(__cdecl*)(const wchar_t*, const GUID*, void**, void*);
static void* NewNameTagCanvas(NameTagFactory factory, int width, int height) {
    const GUID id = {0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    void* canvas = nullptr;
    Require(SUCCEEDED(factory(L"Canvas", &id, &canvas, nullptr)) && canvas, "create name-tag canvas");
    VARIANT magnification{}, format{};
    magnification.vt = format.vt = VT_I4;
    format.lVal = 2;
    using Create = HRESULT(__stdcall*)(void*, int, int, VARIANT, VARIANT);
    Require(SUCCEEDED(ModernEquipmentCompatibility::Method<Create>(canvas, 0x2c)(
        canvas, width, height, magnification, format)), "allocate name-tag RGBA canvas");
    // Canvas.dll allocation leaves pixels uninitialized; its erase sentinel is
    // 0x00FFFFFF (zero merely blends transparent color into the existing data).
    using Rect = HRESULT(__stdcall*)(void*, int, int, int, int, unsigned);
    Require(SUCCEEDED(ModernEquipmentCompatibility::Method<Rect>(canvas, 0x8c)(
        canvas, 0, 0, width, height, 0x00ffffff)), "clear name-tag comparison canvas");
    return canvas;
}
static void NameTagCopy(void* destination, int x, int y, void* source) {
    VARIANT alpha{}; alpha.vt = VT_I4; alpha.lVal = 255;
    using Copy = HRESULT(__stdcall*)(void*, int, int, void*, VARIANT);
    Require(SUCCEEDED(ModernEquipmentCompatibility::Method<Copy>(destination, 0x80)(
        destination, x, y, source, alpha)), "compose name-tag piece");
}
static std::vector<unsigned> NameTagPixels(void* canvas, int width, int height) {
    std::vector<unsigned> pixels;
    using Pixel = HRESULT(__stdcall*)(void*, int, int, unsigned*);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        unsigned pixel = 0;
        Require(SUCCEEDED(ModernEquipmentCompatibility::Method<Pixel>(canvas, 0x88)(canvas, x, y, &pixel)),
            "read composed name-tag pixel");
        pixels.push_back(pixel);
    }
    return pixels;
}
static void CheckNameTagPixels(NameTagFactory factory, bool patched) {
    using namespace ModernEquipmentCompatibility;
    for (int id : {370, 375, 217}) {
        void* pieces[3]{};
        int widths[3]{}, heights[3]{}, origins[3]{};
        int maxY = 0, bottom = 0;
        for (int i = 0; i < 3; ++i) {
            wchar_t path[64]; swprintf_s(path, L"UI/NameTag.img/%d/%c", id, L"wce"[i]);
            pieces[i] = LoadCanvas(path);
            Require(pieces[i] && Dimensions(pieces[i], widths[i], heights[i]), "load real name-tag pieces");
            origins[i] = NameTagOriginFix::getOriginY(pieces[i]);
            maxY = (std::max)(maxY, origins[i]);
            bottom = (std::max)(bottom, heights[i] - origins[i]);
        }
        Require(maxY - origins[2] == (id == 370 ? 3 : id == 375 ? 4 : 0), "known name-tag alignment offsets");
        for (int centerWidth : {24, 60, 120, 198}) {
            centerWidth = (centerWidth + widths[1] - 1) / widths[1] * widths[1];
            int width = widths[0] + centerWidth + widths[2], height = maxY + bottom;
            void* actual = NewNameTagCanvas(factory, width, height);
            void* reference = NewNameTagCanvas(factory, width, height);
            for (void* canvas : {actual, reference}) {
                NameTagCopy(canvas, 0, maxY - origins[0], pieces[0]);
                for (int x = 0; x < centerWidth; x += widths[1])
                    NameTagCopy(canvas, widths[0] + x, maxY - origins[1], pieces[1]);
            }
            NameTagCopy(reference, width - widths[2], maxY - origins[2], pieces[2]);
            Require(SUCCEEDED(InvokeNameTagRight(actual, width - widths[2], maxY, pieces[2])),
                "execute actual native name-tag Copy call");
            const auto actualPixels = NameTagPixels(actual, width, height);
            const auto referencePixels = NameTagPixels(reference, width, height);
            const bool same = actualPixels == referencePixels;
            if (same != (patched || id == 217)) std::printf("name-tag mismatch: id=%d center=%d patched=%d same=%d actualVisible=%d referenceVisible=%d\n",
                id, centerWidth, patched, same,
                static_cast<int>(std::count_if(actualPixels.begin(), actualPixels.end(), [](unsigned p) { return (p >> 24) != 0; })),
                static_cast<int>(std::count_if(referencePixels.begin(), referencePixels.end(), [](unsigned p) { return (p >> 24) != 0; })));
            Require(same == (patched || id == 217), "original seam reproduced; patched pixels match origin-aligned reference");
            static_cast<IUnknown*>(actual)->Release();
            static_cast<IUnknown*>(reference)->Release();
        }
        for (void* piece : pieces) static_cast<IUnknown*>(piece)->Release();
    }
}
static void CheckNameTagResources(NameTagFactory factory) {
    auto continuation = reinterpret_cast<BYTE*>(nameTagCallSite + 6);
    Require(continuation[0] == 0x85 && continuation[1] == 0xc0, "native name-tag HRESULT check continuation");
    // The copied client is never started; return from this isolated instruction.
    continuation[0] = 0xc3;
    FlushInstructionCache(GetCurrentProcess(), continuation, 1);
    CheckNameTagPixels(factory, false);
    CheckNameTagInstallation();
    CheckNameTagCopyContract();
    CheckNameTagPixels(factory, true);
    std::puts("PASS: name-tag 370/375 seams reproduced and fixed with real PCOM pixels; 217 unchanged; four widths, HRESULT, stack, patch rollback");
}
