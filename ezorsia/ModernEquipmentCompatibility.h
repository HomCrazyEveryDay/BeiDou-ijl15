#pragma once
#include <windows.h>
#include <oleauto.h>
#include <cstring>
#include <cwchar>
#include "Memory.h"
#include "CrashReporter.h"

namespace ModernEquipmentCompatibility {
// v83 CItemInfo::GetItemIconCanvas returns an owning IWzCanvas pointer in out.
// Modern cash weapons can provide only iconRaw. NPC #i requests icon, then
// throws E_POINTER at 0099E5EB if the result is null. Retry the same native
// lookup with its raw selector; native conversion, errors and ownership stay
// intact. In particular, do not replace a valid icon or enable disabled items.
using GetIcon = void**(__thiscall*)(void*, void**, int, int, int);
static auto getIcon = reinterpret_cast<GetIcon>(0x005D3BD8);
static LONG iconReports = 0;
static LONG faceReports = 0;
static LONG capeReports = 0;

template<class F> static F Method(void* object, unsigned offset) {
    return reinterpret_cast<F>((*reinterpret_cast<void***>(object))[offset / 4]);
}

static bool Dimensions(void* canvas, int& width, int& height) {
    using GetDimension = HRESULT(__stdcall*)(void*, int*);
    return canvas && SUCCEEDED(Method<GetDimension>(canvas, 0x40)(canvas, &width))
        && SUCCEEDED(Method<GetDimension>(canvas, 0x48)(canvas, &height));
}

static void* LoadInterface(const wchar_t* name, const GUID& iid) {
    void* rm = *reinterpret_cast<void**>(0x00BF14E8);
    if (!rm) return nullptr;
    BSTR path = SysAllocString(name);
    if (!path) return nullptr;
    VARIANT missing{}, value{};
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    using GetObject = HRESULT(__stdcall*)(void*, BSTR, VARIANT, VARIANT, VARIANT*);
    const HRESULT hr = Method<GetObject>(rm, 0x1c)(rm, path, missing, missing, &value);
    SysFreeString(path);
    IUnknown* object = value.vt == VT_UNKNOWN ? value.punkVal
        : value.vt == VT_DISPATCH ? value.pdispVal : nullptr;
    void* canvas = nullptr;
    if (SUCCEEDED(hr) && object) object->QueryInterface(iid, &canvas);
    VariantClear(&value);
    return canvas;
}

static void* LoadCanvas(const wchar_t* name) {
    static const GUID iid = {0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    return LoadInterface(name, iid);
}

// v83 only builds the 128-bit pet mask for the first 100 pet outfits.
// Its membership check also indexes a 16-byte stack mask by petId % 1000,
// so modern pet IDs can write past that mask. Use the actual full-ID WZ
// mapping for newer outfits/pets; unrelated equipment keeps native behavior.
using PetMatch = int(__thiscall*)(void*, int);
using ReadSecureId = int(__cdecl*)(void*, int);
static auto petMatch = reinterpret_cast<PetMatch>(0x0046D408);
static auto readSecureId = reinterpret_cast<ReadSecureId>(0x00416563);
static LONG petReports = 0;

static int MatchPetResource(int equipmentId, int petId) {
    if (equipmentId / 10000 != 180 || petId / 10000 != 500) return 0;
    wchar_t path[96];
    swprintf_s(path, L"Character/PetEquip/%08d.img/%d", equipmentId, petId);
    static const GUID iid = {0x986515d9,0x0a0b,0x4929,{0x8b,0x4f,0x71,0x86,0x82,0x17,0x7b,0x92}};
    void* property = LoadInterface(path, iid);
    if (!property) return 0;
    static_cast<IUnknown*>(property)->Release();
    return 1;
}

static int __fastcall PetEquipment(void* self, void*, int petId) {
    const int equipmentId = readSecureId(self, *reinterpret_cast<int*>(static_cast<BYTE*>(self) + 8));
    if (equipmentId / 10000 != 180 ||
        (equipmentId >= 1802000 && equipmentId < 1802100 && petId >= 5000000 && petId < 5000128))
        return petMatch(self, petId);
    const DWORD error = GetLastError();
    const int allowed = MatchPetResource(equipmentId, petId);
    if (InterlockedIncrement(&petReports) <= 24)
        CrashReporter::RecordEvent("equipment.pet", "equipmentId=%d petId=%d allowed=%d", equipmentId, petId, allowed);
    SetLastError(error);
    return allowed;
}

static const BYTE petMaskOriginal[] = {0x99,0x8b,0xcf,0xf7,0xf9,0x83,0xfa,0x64,0x0f,0x8d,0xed,0x01,0x00,0x00};
static bool WritePetMaskBound(bool install) {
    BYTE patch[sizeof(petMaskOriginal)] = {0x2d,0,0,0,0,0x83,0xf8,0x64,0x0f,0x83,0xed,0x01,0x00,0x00};
    const int firstOutfit = 1802000;
    std::memcpy(patch + 1, &firstOutfit, sizeof(firstOutfit));
    auto site = reinterpret_cast<BYTE*>(0x005CD155);
    const BYTE* expected = install ? petMaskOriginal : patch;
    const BYTE* replacement = install ? patch : petMaskOriginal;
    if (std::memcmp(site, expected, sizeof(patch))) return false;
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(site, replacement, sizeof(patch));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch)) != FALSE;
    if (!flushed) {
        std::memcpy(site, expected, sizeof(patch));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
    }
    const bool restored = VirtualProtect(site, sizeof(patch), protection, &ignored) != FALSE;
    return flushed && restored;
}

static bool InstallPetEquipment() {
    const BYTE expected[] = {0x55,0x8b,0xec,0x83,0xec,0x20,0x56,0x8b,0xf1};
    if (std::memcmp(reinterpret_cast<void*>(petMatch), expected, sizeof(expected))) return false;
    // Preserve the original 1802000..1802099 outfits. Later IDs ending in
    // 000..099 must not enter the old parser merely through modulo collision.
    if (!WritePetMaskBound(true)) return false;
    if (Memory::SetHook(true, reinterpret_cast<void**>(&petMatch), PetEquipment)) return true;
    WritePetMaskBound(false);
    return false;
}

// Observe the original compositor without changing its arguments or result.
// Its output is ZList<ZRef<PetActionFrame>>: count +8, head +12,
// ZRef value +4, and the frame's owning canvas pointer +12.
using LoadPetAction = void(__thiscall*)(void*, void*, int, int, void*);
static auto loadPetAction = reinterpret_cast<LoadPetAction>(0x0040E1A5);
static LONG petRenderReports = 0;
static bool PetFrameSummary(void* output, int& frames, void*& canvas) {
    __try {
        auto list = static_cast<BYTE*>(output);
        frames = *reinterpret_cast<int*>(list + 8);
        if (frames <= 0 || frames > 1000) return false;
        auto head = *reinterpret_cast<BYTE**>(list + 12);
        if (!head) return false;
        auto frame = *reinterpret_cast<BYTE**>(head + 4);
        if (!frame) return false;
        canvas = *reinterpret_cast<void**>(frame + 12);
        return canvas != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void __fastcall PetAction(void* self, void*, void* pet, int action, int equipmentId, void* output) {
    const int petId = pet ? *static_cast<int*>(pet) : 0;
    const bool trace = petId >= 5000128 && petId < 5010000
        && InterlockedIncrement(&petRenderReports) <= 24;
    if (trace) {
        const DWORD error = GetLastError();
        CrashReporter::RecordEvent("equipment.pet-render", "stage=begin petId=%d action=%d equipmentId=%d", petId, action, equipmentId);
        SetLastError(error);
    }
    loadPetAction(self, pet, action, equipmentId, output);
    if (trace) {
        const DWORD error = GetLastError();
        int frames = -1, width = 0, height = 0;
        void* canvas = nullptr;
        const bool valid = PetFrameSummary(output, frames, canvas);
        if (valid) Dimensions(canvas, width, height);
        static const wchar_t* actions[] = {L"move",L"stand0",L"stand1",L"jump",L"fly",L"hungry",L"rest0",L"rest1",L"hang"};
        void* base = nullptr;
        if (action >= 0 && action < 9) {
            wchar_t path[96];
            swprintf_s(path, L"Item/Pet/%d.img/%s/0", petId, actions[action]);
            base = LoadCanvas(path);
        }
        // Native fallback copies the base frame list; composition creates a
        // distinct canvas. Compare COM identity, not arbitrary interface addresses.
        IUnknown* actualIdentity = nullptr;
        IUnknown* baseIdentity = nullptr;
        static const GUID unknown = {0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
        if (canvas) static_cast<IUnknown*>(canvas)->QueryInterface(unknown, reinterpret_cast<void**>(&actualIdentity));
        if (base) static_cast<IUnknown*>(base)->QueryInterface(unknown, reinterpret_cast<void**>(&baseIdentity));
        const int sameBase = actualIdentity && baseIdentity ? actualIdentity == baseIdentity : -1;
        CrashReporter::RecordEvent("equipment.pet-render", "stage=end petId=%d action=%d equipmentId=%d frames=%d canvas=%d size=%dx%d sameBase=%d", petId, action, equipmentId, frames, valid, width, height, sameBase);
        if (actualIdentity) actualIdentity->Release();
        if (baseIdentity) baseIdentity->Release();
        if (base) static_cast<IUnknown*>(base)->Release();
        SetLastError(error);
    }
}
static bool InstallPetRenderingTrace() {
    const BYTE expected[] = {0xb8,0x5f,0x6c,0xa7,0x00,0xe8,0xe9,0x29,0x65,0x00};
    return !std::memcmp(reinterpret_cast<void*>(loadPetAction), expected, sizeof(expected))
        && Memory::SetHook(true, reinterpret_cast<void**>(&loadPetAction), PetAction);
}

// Called only at the two default-canvas assignments inside LoadFaceAction.
// Preserve real expression artwork; replace a missing/1x1 legacy placeholder
// only when the same accessory has a real info/image canvas. The full canvas
// carries the original origin, brow anchor and z. No IMG or WZ object is edited.
static void __stdcall FaceImage(void** canvas, int itemId, int faceId) {
    if (itemId < 1010000 || itemId >= 1020000) return;
    const DWORD error = GetLastError();
    int width = 0, height = 0;
    const bool valid = Dimensions(*canvas, width, height);
    bool recovered = false;
    if (!*canvas || (valid && width == 1 && height == 1)) {
        wchar_t path[96];
        swprintf_s(path, L"Character/Accessory/%08d.img/info/image", itemId);
        void* image = LoadCanvas(path);
        int imageWidth = 0, imageHeight = 0;
        if (Dimensions(image, imageWidth, imageHeight) && imageWidth > 0 && imageHeight > 0
            && (imageWidth > 1 || imageHeight > 1)) {
            if (*canvas) static_cast<IUnknown*>(*canvas)->Release();
            *canvas = image;
            recovered = true;
        } else if (image) {
            static_cast<IUnknown*>(image)->Release();
        }
    }
    if ((recovered || itemId == 1012823 || itemId == 1012634)
        && InterlockedIncrement(&faceReports) <= 24) {
        CrashReporter::RecordEvent("equipment.face",
            "itemId=%d faceId=%d legacy=%dx%d infoImage=%d", itemId, faceId, width, height,
            recovered ? 1 : 0);
    }
    SetLastError(error);
}

static DWORD assignCanvas = 0x0041E42B;
__declspec(naked) static void AssignFaceCanvas() {
    __asm {
        // Preserve the original thiscall stack and HRESULT. EBP is the
        // caller's LoadFaceAction frame, with the IDs saved before iteration.
        push dword ptr [esp + 4]
        call dword ptr [assignCanvas]
        test eax, eax
        js done
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push dword ptr [ebp - 138h]
        push dword ptr [ebp - 130h]
        lea eax, [ebp - 14h]
        push eax
        call FaceImage
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
    done:
        ret 4
    }
}

static bool InstallFaceImages() {
    const BYTE first[] = {0xe8,0x74,0x62,0x01,0x00};
    const BYTE second[] = {0xe8,0x0d,0x53,0x01,0x00};
    if (std::memcmp(reinterpret_cast<void*>(0x004081B2), first, sizeof(first))
        || std::memcmp(reinterpret_cast<void*>(0x00409119), second, sizeof(second))) return false;
    auto a = reinterpret_cast<BYTE*>(0x004081B2);
    auto b = reinterpret_cast<BYTE*>(0x00409119);
    DWORD protectionA = 0, protectionB = 0, ignored = 0;
    // This runs during startup, before either avatar call site executes.
    // Acquire both pages before writing either call; a denied protection
    // change must leave the original instructions intact.
    if (!VirtualProtect(a, 5, PAGE_EXECUTE_READWRITE, &protectionA)) return false;
    if (!VirtualProtect(b, 5, PAGE_EXECUTE_READWRITE, &protectionB)) {
        VirtualProtect(a, 5, protectionA, &ignored);
        return false;
    }
    const DWORD displacementA = reinterpret_cast<DWORD>(AssignFaceCanvas) - 0x004081B7;
    const DWORD displacementB = reinterpret_cast<DWORD>(AssignFaceCanvas) - 0x0040911E;
    std::memcpy(a + 1, &displacementA, 4);
    std::memcpy(b + 1, &displacementB, 4);
    const bool flushedA = FlushInstructionCache(GetCurrentProcess(), a, 5) != FALSE;
    const bool flushedB = FlushInstructionCache(GetCurrentProcess(), b, 5) != FALSE;
    if (!flushedA || !flushedB) {
        std::memcpy(a, first, 5);
        std::memcpy(b, second, 5);
        FlushInstructionCache(GetCurrentProcess(), a, 5);
        FlushInstructionCache(GetCurrentProcess(), b, 5);
    }
    const bool restoredA = VirtualProtect(a, 5, protectionA, &ignored) != FALSE;
    const bool restoredB = VirtualProtect(b, 5, protectionB, &ignored) != FALSE;
    return flushedA && flushedB && restoredA && restoredB;
}

static void** __fastcall ItemIcon(void* self, void*, void** out,
    int itemId, int useIcon, int disabled) {
    const bool traceCape = itemId == 1103568 && InterlockedIncrement(&capeReports) <= 8;
    if (traceCape) {
        const DWORD error = GetLastError();
        CrashReporter::RecordEvent("equipment.icon", "itemId=%d stage=begin useIcon=%d disabled=%d",
            itemId, useIcon, disabled);
        SetLastError(error);
    }
    void** result = getIcon(self, out, itemId, useIcon, disabled);
    if (out && !*out && useIcon) {
        result = getIcon(self, out, itemId, 0, disabled);
        if (InterlockedIncrement(&iconReports) <= 24) {
            const DWORD error = GetLastError();
            CrashReporter::RecordEvent("equipment.icon",
                "itemId=%d disabled=%d iconRaw=%d", itemId, disabled, *out ? 1 : 0);
            SetLastError(error);
        }
    }
    if (traceCape) {
        const DWORD error = GetLastError();
        CrashReporter::RecordEvent("equipment.icon", "itemId=%d stage=end canvas=%d",
            itemId, out && *out ? 1 : 0);
        SetLastError(error);
    }
    return result;
}

static bool Install() {
    const auto base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if (reinterpret_cast<DWORD>(base) != 0x00400000) return false;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != 0x4B7C15C9
        || nt->OptionalHeader.SizeOfImage != 0xA94000) return false;
    const BYTE expected[] = {0xb8,0x47,0x47,0xa9,0x00,0xe8,0xb6,0xcf,0x48,0x00};
    if (std::memcmp(reinterpret_cast<void*>(getIcon), expected, sizeof(expected))) return false;
    const bool icon = Memory::SetHook(true, reinterpret_cast<void**>(&getIcon), ItemIcon);
    const bool face = InstallFaceImages();
    const bool pet = InstallPetEquipment();
    const bool petTrace = InstallPetRenderingTrace();
    CrashReporter::RecordEvent("equipment.compat", "install icon=%d faceImage=%d petEquipment=%d petRenderTrace=%d", icon, face, pet, petTrace);
    return icon && face && pet;
}
}
