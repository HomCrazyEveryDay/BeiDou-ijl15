#pragma once
#include <windows.h>
#include <oleauto.h>
#include <cstring>
#include "CrashReporter.h"
#pragma comment(lib, "oleaut32.lib")

// Narrow v83 guards for the player dumps at 00401D52, 005DAE07 and
// CANVAS+ED28 (008DA50B). Do not catch arbitrary access violations.
namespace ResourceReadGuards {
static LONG avatarReports = 0, itemReports = 0, canvasReports = 0;
static DWORD avatarResume = 0x00401D44, avatarFallback = 0x00401D50, avatarSkip = 0x00401DBE;
static DWORD itemResume = 0x005DAD83, itemReturn = 0x005DADE1;

static const wchar_t* __stdcall OriginalAvatarSlots(const BYTE* part) {
    const auto data = *reinterpret_cast<const BYTE* const*>(part + 0x20);
    // 00774C21 stores the equipment's vslot at data+0C. Borrow its BSTR
    // only for this sort iteration: the part owns it throughout the call.
    // Do not persist a guessed derived mask or alter the native refcounts.
    const auto holder = *reinterpret_cast<const wchar_t* const* const*>(data + 0x0c);
    const wchar_t* slots = holder ? *holder : nullptr;
    if (InterlockedIncrement(&avatarReports) <= 8) {
        const DWORD error = GetLastError();
        CrashReporter::RecordEvent("resource.guard",
            "avatar missing_slot part=%p data=%p canvas=%p z=%d fallback=%s",
            part, data, *reinterpret_cast<void* const*>(data + 0x30),
            *reinterpret_cast<const int*>(data + 0x24), slots ? "vslot" : "skip_part");
        SetLastError(error);
    }
    return slots;
}

__declspec(naked) static void AvatarSlots() {
    __asm {
        // Hook before the holder lookup, not at 00401D52: the native pair
        // loop jumps back to 00401D52 and must retain that instruction.
        mov eax, [esi + 20h]
        mov eax, [eax + 14h]
        pushfd
        test eax, eax
        jz missing
        cmp dword ptr [eax], 0
        jne valid
    missing:
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push esi
        call OriginalAvatarSlots
        mov [ebx + 28], eax
        fxrstor [esp]
        mov esp, ebx
        popad
        test eax, eax
        jnz fallback
        popfd
        mov ebx, edx
        jmp dword ptr [avatarSkip]
    fallback:
        popfd
        mov ebx, edx
        jmp dword ptr [avatarFallback]
    valid:
        popfd
        jmp dword ptr [avatarResume]
    }
}

static void __stdcall MissingItemInfo(int itemId, const void* property) {
    if (InterlockedIncrement(&itemReports) <= 8) {
        const DWORD error = GetLastError();
        CrashReporter::RecordEvent("resource.guard",
            "item missing_info itemId=%d property=%p checksum=0 cached=0", itemId, property);
        SetLastError(error);
    }
}

__declspec(naked) static void ItemInfo() {
    __asm {
        pushfd
        cmp dword ptr [ebp - 1Ch], 0
        je missing
        popfd
        // Replay 005DAD7E..005DAD82. Normal CRC and caching stay native.
        mov eax, [ebp - 1Ch]
        push ecx
        push ecx
        jmp dword ptr [itemResume]
    missing:
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push dword ptr [ebp - 14h]
        push dword ptr [ebp + 8]
        call MissingItemInfo
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        // The ZRef at EBP-20 is null, but the WZ property still owns a COM
        // reference. Match 005DADB2's cleanup, then bypass the cache insert
        // at 005DADD2 so a transient read failure can recover next time.
        mov dword ptr [ebp - 4], -1
        mov eax, [ebp - 14h]
        test eax, eax
        jz released
        mov ecx, [eax]
        push eax
        call dword ptr [ecx + 8]
    released:
        mov dword ptr [ebp - 10h], 0
        jmp dword ptr [itemReturn]
    }
}

template<class F> static F Method(void* object, unsigned offset) {
    return reinterpret_cast<F>((*reinterpret_cast<void***>(object))[offset / 4]);
}
static void* LoadBracket() {
    void* rm = *reinterpret_cast<void**>(0x00BF14E8);
    if (!rm) return nullptr;
    BSTR path = SysAllocString(L"UI/StatusBar.img/number/Lbracket");
    if (!path) return nullptr;
    VARIANT missing{}, value{};
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    using GetObject = HRESULT(__stdcall*)(void*, BSTR, VARIANT, VARIANT, VARIANT*);
    const HRESULT hr = Method<GetObject>(rm, 0x1c)(rm, path, missing, missing, &value);
    SysFreeString(path);
    static const GUID iid = {0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    IUnknown* object = value.vt == VT_UNKNOWN ? value.punkVal
        : value.vt == VT_DISPATCH ? value.pdispVal : nullptr;
    void* canvas = nullptr;
    if (SUCCEEDED(hr) && object) object->QueryInterface(iid, &canvas);
    VariantClear(&value);
    return canvas; // Owns the QueryInterface reference.
}
using CopyCanvas = HRESULT(__thiscall*)(void*, int, int, void*, const VARIANT*);
static auto copyCanvas = reinterpret_cast<CopyCanvas>(0x00424BA7);
static auto loadBracket = &LoadBracket;
// Only the UI thread uses this cache. At most one immutable fallback canvas
// is retained; release it as soon as the native lookup succeeds again.
static IUnknown* bracket = nullptr;
static bool attempted = false;
static DWORD lastAttempt = 0;

static HRESULT __fastcall CopyStatusBracket(void* self, void*, int x, int y,
    void* source, const VARIANT* alpha) {
    if (source) {
        if (bracket) { bracket->Release(); bracket = nullptr; }
        attempted = false;
        return copyCanvas(self, x, y, source, alpha);
    }
    const DWORD now = GetTickCount();
    if (!bracket && (!attempted || now - lastAttempt >= 1000)) {
        attempted = true;
        lastAttempt = now;
        bracket = static_cast<IUnknown*>(loadBracket());
        if (InterlockedIncrement(&canvasReports) <= 8)
            CrashReporter::RecordEvent("resource.guard",
                "statusbar missing_canvas path=UI/StatusBar.img/number/Lbracket recovered=%d",
                bracket ? 1 : 0);
    }
    // If rereading also fails, omit only this punctuation for this draw.
    // Never call Canvas::Copy with a null source or suppress other errors.
    return bracket ? copyCanvas(self, x, y, bracket, alpha) : S_FALSE;
}

static bool Patch(DWORD address, const BYTE* expected, SIZE_T size, void* target, BYTE opcode) {
    auto site = reinterpret_cast<BYTE*>(address);
    if (std::memcmp(site, expected, size)) return false;
    DWORD protection = 0;
    if (!VirtualProtect(site, size, PAGE_EXECUTE_READWRITE, &protection)) return false;
    const DWORD displacement = reinterpret_cast<DWORD>(target) - address - 5;
    site[0] = opcode;
    std::memcpy(site + 1, &displacement, sizeof(displacement));
    if (size > 5) std::memset(site + 5, 0x90, size - 5);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, size) != FALSE;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(site, size, protection, &ignored) != FALSE;
    return flushed && restored;
}

static void Install() {
    const auto base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if (reinterpret_cast<DWORD>(base) != 0x00400000) return;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != 0x4B7C15C9
        || nt->OptionalHeader.SizeOfImage != 0xA94000) return;
    const BYTE avatar[] = {0x8b,0x46,0x20,0x8b,0x40,0x14};
    const BYTE item[] = {0x8b,0x45,0xe4,0x51,0x51};
    const BYTE canvas[] = {0xe8,0x97,0xa6,0xb4,0xff};
    const bool a = Patch(0x00401D3E, avatar, sizeof(avatar), AvatarSlots, 0xe9);
    const bool i = Patch(0x005DAD7E, item, sizeof(item), ItemInfo, 0xe9);
    const bool c = Patch(0x008DA50B, canvas, sizeof(canvas), CopyStatusBracket, 0xe8);
    CrashReporter::RecordEvent("resource.guard", "install avatar=%d item=%d statusbar=%d", a, i, c);
}
}
