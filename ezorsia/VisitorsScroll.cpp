#include "stdafx.h"
#include "VisitorsScroll.h"
#include "ClientLog.h"
#include "ResourceVariant.h"
#include <cstring>
#include <cwchar>

namespace {
#ifdef VISITORS_SCROLL_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
using NativeMatch = int(__cdecl*)(int, int);
const auto nativeMatch = reinterpret_cast<NativeMatch>(Native(0x004F5497));
const DWORD nativeUseCheck = Native(0x004F0D45);
const DWORD readItemId = Native(0x0042873D);
bool installed = false;
LONG matchReports = 0, useReports = 0;

bool IsMaterial(int itemId) {
    return (itemId >= 2046215 && itemId <= 2046218)
        || (itemId >= 2046400 && itemId <= 2046403)
        || itemId == 2471000 || (itemId >= 2490000 && itemId <= 2490003);
}

const wchar_t* EquipmentFolder(int itemId) {
    if (itemId >= 1003115 && itemId <= 1003119) return L"Cap";
    if (itemId >= 1052277 && itemId <= 1052281) return L"Longcoat";
    if (itemId >= 1072449 && itemId <= 1072453) return L"Shoes";
    if (itemId >= 1082277 && itemId <= 1082281) return L"Glove";
    return nullptr;
}

HRESULT ResourceValue(BSTR path, VARIANT* value) {
    __try {
        void* manager = *reinterpret_cast<void**>(Native(0x00BF14E8));
        if (!manager) return E_POINTER;
        VARIANT missing{};
        missing.vt = VT_ERROR;
        missing.scode = DISP_E_PARAMNOTFOUND;
        using GetObject = HRESULT(__stdcall*)(void*, BSTR, VARIANT, VARIANT, VARIANT*);
        const auto methods = *reinterpret_cast<void***>(manager);
        return reinterpret_cast<GetObject>(methods[7])(manager, path, missing, missing, value);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return E_UNEXPECTED; }
}

int Category(int itemId) {
    wchar_t path[112]{};
    if (IsMaterial(itemId))
        swprintf_s(path, L"Item/Consume/%04d.img/%08d/info/enchantCategory", itemId / 10000, itemId);
    else {
        const auto folder = EquipmentFolder(itemId);
        if (!folder) return -1;
        swprintf_s(path, L"Character/%s/%08d.img/info/enchantCategory", folder, itemId);
    }
    BSTR name = SysAllocString(path);
    if (!name) return -1;
    VARIANT value{};
    const HRESULT result = ResourceValue(name, &value);
    SysFreeString(name);
    int category = -1;
    if (SUCCEEDED(result)) {
        if (value.vt == VT_I4) category = value.lVal;
        else if (value.vt == VT_I2) category = value.iVal;
    }
    ResourceVariant::Clear(&value);
    return category;
}

int __cdecl CanApply(int material, int equipment) {
    if (!IsMaterial(material)) return nativeMatch(material, equipment);
    const DWORD error = GetLastError();
    const bool targetAllowed = EquipmentFolder(equipment) != nullptr;
    const int sourceCategory = targetAllowed ? Category(material) : -1;
    const int targetCategory = targetAllowed ? Category(equipment) : -1;
    const int allowed = targetAllowed && sourceCategory == 2 && targetCategory == 2;
    if (InterlockedIncrement(&matchReports) <= 32)
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=visitors_scroll_match material=%d equipment=%d sourceCategory=%d targetCategory=%d allowed=%d",
            material, equipment, sourceCategory, targetCategory, allowed);
    SetLastError(error);
    return allowed;
}

int __stdcall BlockUse(int itemId) {
    if (itemId != 2471000 && (itemId < 2490000 || itemId > 2490003)) return 0;
    const DWORD error = GetLastError();
    if (InterlockedIncrement(&useReports) <= 8)
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=visitors_scroll_double_click_blocked item=%d", itemId);
    SetLastError(error);
    return 1;
}

__declspec(naked) void UseCheck() {
    __asm {
        push ecx
        push edx
        lea ecx, [esi+0ch]
        call dword ptr [readItemId]
        push eax
        call BlockUse
        pop edx
        pop ecx
        test eax, eax
        jnz blocked
        jmp dword ptr [nativeUseCheck]
    blocked:
        ret
    }
}

struct Signature { DWORD address; const char* expected; size_t size; };
struct Patch { Signature signature; void* target; DWORD protection; };
Patch patches[] = {
    {{Native(0x004F4E1C), "\xe8\x76\x06\x00\x00", 5}, CanApply, 0},
    {{Native(0x004F030A), "\xe8\x36\x0a\x00\x00", 5}, UseCheck, 0}
};
const Signature dependencies[] = {
    {Native(0x004F5497), "\x53\x56\x57\x8b\x7c\x24\x10", 7},
    {Native(0x004F030F), "\x85\xc0\x0f\x85\x35\xfa\xff\xff", 8},
    {Native(0x004F4EF4), "\xe8\x28\x43\x51\x00", 5},
    {Native(0x00A09245), "\x6a\x56", 2},
    {Native(0x00A09261), "\xff\x75\x08\x8d\x4d\xe4\xe8\x08\xed\xa1\xff"
        "\xff\x75\x0c\x8d\x4d\xe4\xe8\xfd\xec\xa1\xff"
        "\xff\x75\x10\x8d\x4d\xe4\xe8\xf2\xec\xa1\xff"
        "\xff\x75\x14\x8d\x4d\xe4\xe8\xbc\xd2\x9f\xff", 44}
};

bool Check(const Signature& signature) {
    const auto* actual = reinterpret_cast<const unsigned char*>(signature.address);
    for (size_t offset = 0; offset < signature.size; ++offset) {
        const auto expected = static_cast<unsigned char>(signature.expected[offset]);
        if (actual[offset] == expected) continue;
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=visitors_scroll_install success=0 reason=signature address=%08lX offset=%u expected=%02X actual=%02X",
            signature.address, static_cast<unsigned>(offset), expected, actual[offset]);
        return false;
    }
    return true;
}

bool RestoreProtection(size_t count) {
    bool restored = true;
    while (count) {
        auto& patch = patches[--count];
        DWORD ignored;
        if (!VirtualProtect(reinterpret_cast<void*>(patch.signature.address), patch.signature.size,
                patch.protection, &ignored)) restored = false;
    }
    return restored;
}
}

bool VisitorsScroll::Install(HMODULE module) {
    if (reinterpret_cast<DWORD>(module) != Native(0x00400000)) return false;
    if (installed) return true;
    const auto* base = reinterpret_cast<const unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (image->Signature != IMAGE_NT_SIGNATURE || image->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || image->FileHeader.TimeDateStamp != 0x4B7C15C9 || image->OptionalHeader.SizeOfImage != 0xA94000) return false;
    for (const auto& patch : patches) if (!Check(patch.signature)) return false;
    for (const auto& signature : dependencies) if (!Check(signature)) return false;
    size_t acquired = 0;
    for (auto& patch : patches) {
        if (!VirtualProtect(reinterpret_cast<void*>(patch.signature.address), patch.signature.size,
                PAGE_EXECUTE_READWRITE, &patch.protection)) {
            const DWORD error = GetLastError();
            const bool restored = RestoreProtection(acquired);
            ClientLog::Append(ClientLog::Component::Lifecycle,
                "event=visitors_scroll_install success=0 reason=protect address=%08lX error=%lu protectionRestored=%d",
                patch.signature.address, error, restored);
            return false;
        }
        ++acquired;
    }
    for (const auto& patch : patches) {
        auto* site = reinterpret_cast<unsigned char*>(patch.signature.address);
        const DWORD displacement = reinterpret_cast<DWORD>(patch.target) - patch.signature.address - 5;
        site[0] = 0xE8;
        std::memcpy(site + 1, &displacement, sizeof(displacement));
    }
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), base, image->OptionalHeader.SizeOfImage) != FALSE;
    if (!flushed) {
        for (const auto& patch : patches)
            std::memcpy(reinterpret_cast<void*>(patch.signature.address), patch.signature.expected, patch.signature.size);
        const bool rollbackFlushed = FlushInstructionCache(GetCurrentProcess(), base, image->OptionalHeader.SizeOfImage) != FALSE;
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=visitors_scroll_install success=0 reason=flush rollbackFlushed=%d", rollbackFlushed);
    }
    const bool restored = RestoreProtection(acquired);
    installed = flushed;
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "event=visitors_scroll_install version=1 success=%d patches=2 protectionRestored=%d", installed, restored);
    return installed;
}
