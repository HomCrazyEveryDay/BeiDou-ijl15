#include "stdafx.h"
#include "VisitorsShuttle.h"
#include "ClientLog.h"
#include <cstring>

namespace {
#ifdef VISITORS_SHUTTLE_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
const DWORD vehicleResume = Native(0x008C468B), vehicleAccepted = Native(0x008C46A2);
const DWORD effectResume = Native(0x0093690A), effectTail = Native(0x00936B37);
const DWORD restrictionResume = Native(0x00969E71), restrictionCheck = Native(0x00969FB4);
const DWORD startResume = Native(0x0096A2F8), startCount = Native(0x0096A316);
const DWORD cancelResume = Native(0x0096D8CE), cancelCount = Native(0x0096D8F0);
bool installed = false;
LONG failureLogged = 0, missingResourceLogged = 0;

void* __cdecl ShuttleResource(int job) {
    const int skill = VisitorsShuttle::SkillForJob(job);
    void* database = *reinterpret_cast<void**>(Native(0x00BE78DC));
    using FindSkill = void*(__thiscall*)(void*, int);
    void* resource = skill && database
        ? reinterpret_cast<FindSkill>(Native(0x0075C755))(database, skill) : nullptr;
    if (!resource && InterlockedCompareExchange(&missingResourceLogged, 1, 0) == 0)
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=visitors_shuttle_missing_resource job=%d skill=%d", job, skill);
    return resource;
}

__declspec(naked) void VehicleGate() {
    __asm {
        cmp eax, 1932037
        je accepted
        cmp eax, 1932001
        jmp dword ptr [vehicleResume]
    accepted:
        jmp dword ptr [vehicleAccepted]
    }
}

__declspec(naked) void EffectGate() {
    __asm {
        pushfd
        cmp dword ptr [ebp+8], 1932037
        jne original
        pushad
        push eax
        call ShuttleResource
        add esp, 4
        mov [ebp-10h], eax
        mov dword ptr [esi+1f4ch], 240
        popad
        popfd
        jmp dword ptr [effectTail]
    original:
        popfd
        mov ebx, eax
        mov eax, [ebp+8]
        jmp dword ptr [effectResume]
    }
}

__declspec(naked) void RestrictionGate() {
    __asm {
        pushfd
        pushad
        push eax
        call VisitorsShuttle::IsSkill
        add esp, 4
        test al, al
        jz original
        popad
        popfd
        jmp dword ptr [restrictionCheck]
    original:
        popad
        popfd
        cmp eax, 4faa8eh
        jmp dword ptr [restrictionResume]
    }
}

__declspec(naked) void StartGate() {
    __asm {
        pushfd
        pushad
        push eax
        call VisitorsShuttle::IsSkill
        add esp, 4
        test al, al
        jz original
        popad
        popfd
        jmp dword ptr [startCount]
    original:
        popad
        popfd
        cmp eax, 3ech
        jmp dword ptr [startResume]
    }
}

__declspec(naked) void CancelGate() {
    __asm {
        pushfd
        pushad
        push ebx
        call VisitorsShuttle::IsSkill
        add esp, 4
        test al, al
        jz original
        popad
        popfd
        jmp dword ptr [cancelCount]
    original:
        popad
        popfd
        cmp ebx, 3ech
        jmp dword ptr [cancelResume]
    }
}

struct Patch {
    DWORD address;
    const char* expected;
    size_t size;
    void* target;
    DWORD protection;
};
Patch patches[] = {
    {Native(0x008C4686), "\x3d\xe1\x7a\x1d\x00", 5, VehicleGate, 0},
    {Native(0x00936905), "\x8b\xd8\x8b\x45\x08", 5, EffectGate, 0},
    {Native(0x00969E6C), "\x3d\x8e\xaa\x4f\x00", 5, RestrictionGate, 0},
    {Native(0x0096A2F3), "\x3d\xec\x03\x00\x00", 5, StartGate, 0},
    {Native(0x0096D8C8), "\x81\xfb\xec\x03\x00\x00", 6, CancelGate, 0}
};

bool RestoreProtection(size_t count) {
    bool restored = true;
    while (count) {
        auto& patch = patches[--count];
        DWORD ignored;
        if (!VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size, patch.protection, &ignored))
            restored = false;
    }
    return restored;
}

bool Failure(const char* reason, DWORD address) {
    if (InterlockedCompareExchange(&failureLogged, 1, 0) == 0)
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=visitors_shuttle_install success=0 reason=%s address=%08lX", reason, address);
    return false;
}
}

bool VisitorsShuttle::IsSkill(int skill) {
    return skill == 1065 || skill == 10001065 || skill == 20001065 || skill == 20011065;
}

int VisitorsShuttle::SkillForJob(int job) {
    if (job == 2001 || job == 2200 || (job >= 2210 && job <= 2218)) return 20011065;
    switch (job) {
    case 0: case 100: case 110: case 111: case 112: case 120: case 121: case 122:
    case 130: case 131: case 132: case 200: case 210: case 211: case 212:
    case 220: case 221: case 222: case 230: case 231: case 232:
    case 300: case 310: case 311: case 312: case 320: case 321: case 322:
    case 400: case 410: case 411: case 412: case 420: case 421: case 422:
    case 500: case 510: case 511: case 512: case 520: case 521: case 522:
    case 900: case 910: return 1065;
    case 1000: case 1100: case 1110: case 1111: case 1112:
    case 1200: case 1210: case 1211: case 1212: case 1300: case 1310: case 1311: case 1312:
    case 1400: case 1410: case 1411: case 1412: case 1500: case 1510: case 1511: case 1512:
        return 10001065;
    case 2000: case 2100: case 2110: case 2111: case 2112: return 20001065;
    default: return 0;
    }
}

bool VisitorsShuttle::Install(HMODULE module) {
    if (reinterpret_cast<DWORD>(module) != Native(0x00400000)) return Failure("base", 0);
    if (installed) return true;
    const auto* base = reinterpret_cast<const unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000)
        return Failure("dos", 0);
    const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (image->Signature != IMAGE_NT_SIGNATURE || image->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || image->FileHeader.TimeDateStamp != 0x4B7C15C9 || image->OptionalHeader.SizeOfImage != 0xA94000)
        return Failure("image", 0);
    for (const auto& patch : patches)
        if (std::memcmp(reinterpret_cast<void*>(patch.address), patch.expected, patch.size))
            return Failure("signature", patch.address);
    size_t acquired = 0;
    for (auto& patch : patches) {
        if (!VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size, PAGE_EXECUTE_READWRITE, &patch.protection)) {
            RestoreProtection(acquired);
            return Failure("protect", patch.address);
        }
        ++acquired;
    }
    for (const auto& patch : patches) {
        auto* site = reinterpret_cast<unsigned char*>(patch.address);
        const DWORD displacement = reinterpret_cast<DWORD>(patch.target) - patch.address - 5;
        std::memset(site, 0x90, patch.size);
        site[0] = 0xE9;
        std::memcpy(site + 1, &displacement, sizeof(displacement));
    }
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), base, image->OptionalHeader.SizeOfImage) != FALSE;
    if (!flushed) {
        for (const auto& patch : patches)
            std::memcpy(reinterpret_cast<void*>(patch.address), patch.expected, patch.size);
        FlushInstructionCache(GetCurrentProcess(), base, image->OptionalHeader.SizeOfImage);
    }
    const bool restored = RestoreProtection(acquired);
    installed = flushed;
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "event=visitors_shuttle_install version=1 success=%d patches=5 protectionRestored=%d", installed, restored);
    return installed;
}
