#include "stdafx.h"
#include "MainPetInventoryMark.h"
#include "CrashReporter.h"
#include "DreamCanvas.h"
#include "ResourceVariant.h"
#include <cstring>

namespace {
BYTE* installedClient = nullptr;
DWORD drawResume = 0;
void** resourceManager = nullptr;
void** gameContext = nullptr;
using Update = void(__thiscall*)(void*);
using Invalidate = void(__thiscall*)(void*, const RECT*);
Update nativeUpdate = nullptr;
Invalidate invalidate = nullptr;
void* lastWindow = nullptr;
ULONGLONG lastLeader = 0;
LONG resourceFailures = 0;

ULONGLONG Serial(const void* object, size_t offset) {
    ULONGLONG value = 0;
    if (object) std::memcpy(&value, static_cast<const BYTE*>(object) + offset, sizeof(value));
    return value;
}

struct CanvasRef {
    void* value = nullptr;
    ~CanvasRef() { DreamCanvas::Release(value); }
};
struct ResourceValue {
    VARIANT value{};
    ~ResourceValue() { ResourceVariant::Clear(&value); }
};

void* LoadMark() {
    if (!resourceManager || !*resourceManager) return nullptr;
    BSTR path = SysAllocString(L"UI/UIWindow.img/Item/bossPetIcon");
    if (!path) return nullptr;
    ResourceValue result;
    VARIANT missing{};
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    using GetObject = HRESULT(__stdcall*)(void*, BSTR, VARIANT, VARIANT, VARIANT*);
    auto manager = *resourceManager;
    const HRESULT hr = DreamCanvas::Method<GetObject>(manager, 0x1c)(
        manager, path, missing, missing, &result.value);
    SysFreeString(path);
    if (FAILED(hr)) return nullptr;
    IUnknown* object = result.value.vt == VT_UNKNOWN ? result.value.punkVal
        : result.value.vt == VT_DISPATCH ? result.value.pdispVal : nullptr;
    static const GUID canvasIid = {0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    void* canvas = nullptr;
    if (object) object->QueryInterface(canvasIid, &canvas);
    return canvas;
}

// CUIItem::Draw has just drawn this item. EDI is its CharacterData, ESI is
// GW_ItemSlotBase, and the slot RECT supplies left/bottom (native 32px icon).
void __stdcall DrawMark(void* window, void* character, void* item, void* destination, int x, int bottom) {
    if (!window || !character || !item || !destination
        || *reinterpret_cast<int*>(static_cast<BYTE*>(window) + 0x5e4) != 5) return;
    using GetType = int(__thiscall*)(void*);
    if (DreamCanvas::Method<GetType>(item, 0x18)(item) != 3) return;
    // CharacterStat::DecodeChange writes the first pet's 64-bit SN at +1B.
    // GW_ItemSlotBase::Decode writes the cash SN at +18. Names/IDs/slots are
    // not identities: two identical pets must never both receive the crown.
    const auto leader = Serial(character, 0x1b);
    if (!leader || leader != Serial(item, 0x18)) return;

    CanvasRef mark{LoadMark()};
    if (!mark.value) {
        if (InterlockedIncrement(&resourceFailures) <= 4)
            CrashReporter::RecordEvent("pet.inventory", "bossPetIcon unavailable; skip mark");
        return;
    }
    LONG originX = 0, originY = 0, width = 0, height = 0;
    using Get = HRESULT(__stdcall*)(void*, LONG*);
    if (FAILED(DreamCanvas::Method<Get>(mark.value, 0x6c)(mark.value, &originX))
        || FAILED(DreamCanvas::Method<Get>(mark.value, 0x74)(mark.value, &originY))
        || FAILED(DreamCanvas::Method<Get>(mark.value, 0x40)(mark.value, &width))
        || FAILED(DreamCanvas::Method<Get>(mark.value, 0x48)(mark.value, &height))
        || width <= 0 || height < width) return;
    VARIANT alpha{};
    alpha.vt = VT_I4;
    alpha.lVal = 255; // Blend transparent pixels over the existing pet icon.
    using Copy = HRESULT(__stdcall*)(void*, int, int, void*, VARIANT);
    const HRESULT hr = DreamCanvas::Method<Copy>(destination, 0x80)(
        // The original art has a 34px square border and a 4px crown above it.
        destination, x - originX, bottom - 32 - originY - (height - width), mark.value, alpha);
    if (FAILED(hr) && InterlockedIncrement(&resourceFailures) <= 4)
        CrashReporter::RecordEvent("pet.inventory", "mark copy failed hr=%08lX", hr);
}

__declspec(naked) void DrawBridge() {
    __asm {
        pushfd
        pushad
        push dword ptr [ebp-34h]
        push dword ptr [ebp-40h]
        push dword ptr [ebp-10h]
        push esi
        push edi
        push ebx
        call DrawMark
        popad
        popfd
        mov eax, [ebx+5e4h]
        jmp dword ptr [drawResume]
    }
}

void __fastcall UpdateMark(void* window, void*) {
    nativeUpdate(window);
    auto context = gameContext ? *gameContext : nullptr;
    auto character = context ? *reinterpret_cast<void**>(static_cast<BYTE*>(context) + 0x20b8) : nullptr;
    const auto leader = Serial(character, 0x1b);
    // Redraw on summon/recall/leader change, including while the bag stays open.
    // Keep no character/item/COM references between UI callbacks.
    if (window != lastWindow || leader != lastLeader) {
        lastWindow = window;
        lastLeader = leader;
        invalidate(window, nullptr);
    }
}
}

bool MainPetInventoryMark::Install(HMODULE client) {
    auto base = reinterpret_cast<BYTE*>(client);
    if (installedClient) return installedClient == base;
    if (!base) return false;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->FileHeader.TimeDateStamp != 0x4B7C15C9 || nt->OptionalHeader.SizeOfImage != 0xA94000) return false;
    const BYTE draw[] = {0x8b,0x83,0xe4,0x05,0,0};
    const BYTE update[] = {0xe8,0xb8,0x23,0x1c,0};
    const BYTE petSerial[] = {0x6a,0x08,0x8d,0x46,0x18,0x50,0x8b,0xcf,0xe8,0x85,0xeb,0xf4,0xff};
    const BYTE leaderSerial[] = {0x53,0x8d,0x46,0x1b,0x50,0x8b,0xcf,0xe8,0x45,0xf2,0xf4,0xff};
    const BYTE drawInputs[] = {0xff,0x75,0xcc,0x8d,0x45,0xf0,0xff,0x75,0xc0,0xff,0x75,0xec};
    if (std::memcmp(base+0x41deee,draw,sizeof(draw)) || std::memcmp(base+0x41e2c1,update,sizeof(update))
        || std::memcmp(base+0xe36c5,petSerial,sizeof(petSerial))
        || std::memcmp(base+0xe3006,leaderSerial,sizeof(leaderSerial))
        || std::memcmp(base+0x41de9b,drawInputs,sizeof(drawInputs))) return false;
    struct Patch { BYTE* site; const BYTE* before; SIZE_T size; BYTE opcode; void* target; DWORD protection; };
    Patch patches[] = {{base+0x41deee,draw,sizeof(draw),0xe9,DrawBridge,0},
                       {base+0x41e2c1,update,sizeof(update),0xe8,UpdateMark,0}};
    DWORD ignored = 0;
    unsigned prepared = 0;
    for (auto& patch : patches) {
        if (!VirtualProtect(patch.site,patch.size,PAGE_EXECUTE_READWRITE,&patch.protection)) {
            for (unsigned i=0;i<prepared;++i)
                VirtualProtect(patches[i].site,patches[i].size,patches[i].protection,&ignored);
            return false;
        }
        ++prepared;
    }
    drawResume = reinterpret_cast<DWORD>(base+0x41def4);
    resourceManager = reinterpret_cast<void**>(base+0x7f14e8);
    gameContext = reinterpret_cast<void**>(base+0x7e7918);
    nativeUpdate = reinterpret_cast<Update>(base+0x5e067e);
    invalidate = reinterpret_cast<Invalidate>(base+0x5e04c9);
    bool ok = true;
    for (auto& patch : patches) {
        std::memset(patch.site,0x90,patch.size);
        patch.site[0] = patch.opcode;
        const DWORD relative = reinterpret_cast<DWORD>(patch.target)-reinterpret_cast<DWORD>(patch.site)-5;
        std::memcpy(patch.site+1,&relative,4);
        if (!FlushInstructionCache(GetCurrentProcess(),patch.site,patch.size)) ok = false;
    }
    if (!ok) for (auto& patch : patches) {
        std::memcpy(patch.site,patch.before,patch.size);
        FlushInstructionCache(GetCurrentProcess(),patch.site,patch.size);
    }
    for (auto& patch : patches) VirtualProtect(patch.site,patch.size,patch.protection,&ignored);
    if (ok) installedClient = base;
    return ok;
}
