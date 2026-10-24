#include "stdafx.h"
#include "VisitorsMobClimb.h"
#include "ClientLog.h"
#include <cstring>

namespace {
static_assert(sizeof(void*) == 4, "Visitors mob actions require x86");
#ifdef VISITORS_MOB_CLIMB_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif

constexpr int nativeActionCount = 40;
constexpr int actionCount = 42;
constexpr size_t actionStride = 0x14;
using Name = void*;
Name actionNames[actionCount]{};
using CopyName = void*(__thiscall*)(Name*, const Name*);
using CreateName = void*(__thiscall*)(Name*, const wchar_t*);
using ReleaseName = void(__thiscall*)(Name*);
using AtExit = int(__cdecl*)(void(__cdecl*)());
using HasFrames = int(__thiscall*)(void*, int);
using ClearFrames = void(__thiscall*)(void*);
using ReadTemplateId = unsigned(__cdecl*)(const void*, unsigned);
const auto copyName = reinterpret_cast<CopyName>(Native(0x00405210));
const auto createName = reinterpret_cast<CreateName>(Native(0x00403382));
const auto releaseName = reinterpret_cast<ReleaseName>(Native(0x00403394));
const auto nativeAtExit = reinterpret_cast<AtExit>(Native(0x00A606AE));
const auto hasFrames = reinterpret_cast<HasFrames>(Native(0x00671929));
const auto clearFrames = reinterpret_cast<ClearFrames>(Native(0x00416B72));
const auto readTemplateId = reinterpret_cast<ReadTemplateId>(Native(0x004E8152));
const DWORD initializeSeh = Native(0x00A824C4);
const DWORD initializeResume = Native(0x004A6C0A);
const DWORD decodeResume = Native(0x00664D48);
const DWORD normalizeResume = Native(0x00671B06);
const DWORD templateLoopResume = Native(0x0067F4CE);
bool armed = false;
bool active = false;
LONG activationAttempted = 0;
LONG failureLogged = 0;
LONG missingLogged[2]{};

template<typename Value> Value Field(const void* object, size_t offset) {
    return *reinterpret_cast<const Value*>(static_cast<const unsigned char*>(object) + offset);
}

void Failure(const char* reason, DWORD address = 0) {
    if (InterlockedCompareExchange(&failureLogged, 1, 0) == 0)
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=visitors_mob_climb_install success=0 reason=%s address=%08lX", reason, address);
}

int ClimbIndex(unsigned raw) {
    if ((raw & ~1u) == 0x50) return 40;
    if ((raw & ~1u) == 0x52) return 41;
    return -1;
}

unsigned AirRaw(unsigned raw) { return 6u | (raw & 1u); }

struct Availability {
    void* mobTemplate;
    unsigned templateId;
    signed char state[2];
};
constexpr size_t availabilityCapacity = 256;
Availability availability[availabilityCapacity]{};
SRWLOCK availabilityLock = SRWLOCK_INIT;
unsigned generation = 0;
size_t replacementSlot = 0;

void __cdecl InvalidateAvailability() {
    AcquireSRWLockExclusive(&availabilityLock);
    std::memset(availability, 0, sizeof(availability));
    ++generation;
    replacementSlot = 0;
    ReleaseSRWLockExclusive(&availabilityLock);
}

void* EffectiveTemplate(void* mob) {
    if (Field<int>(mob, 0x2A4)) {
        void* transformed = Field<void*>(mob, 0x18C);
        if (transformed) return transformed;
    }
    return Field<void*>(mob, 0x188);
}

bool ProbeFrames(void* mob, int action, void* list) {
    try {
        return hasFrames(mob, action) != 0 && Field<unsigned>(list, 8) != 0;
    } catch (...) {
        clearFrames(list);
        return false;
    }
}

bool ExtendedFrames(void* mob, int action) {
    if (!active || !mob || action < nativeActionCount || action >= actionCount) return false;
    void* mobTemplate = EffectiveTemplate(mob);
    auto* cache = Field<unsigned char*>(mob, 0x434);
    if (!mobTemplate || !cache || Field<unsigned>(cache - 4, 0) < actionCount) return false;
    unsigned templateId;
    try {
        templateId = readTemplateId(static_cast<unsigned char*>(mobTemplate) + 0xC,
            Field<unsigned>(mobTemplate, 0x14));
    } catch (...) {
        return false;
    }
    const int kind = action - nativeActionCount;
    AcquireSRWLockExclusive(&availabilityLock);
    size_t slot = 0;
    while (slot < availabilityCapacity && (availability[slot].mobTemplate != mobTemplate
        || availability[slot].templateId != templateId)) ++slot;
    if (slot == availabilityCapacity) {
        slot = replacementSlot++ % availabilityCapacity;
        availability[slot] = {mobTemplate, templateId, {0, 0}};
    }
    auto& entry = availability[slot];
    if (entry.state[kind] == -1 || entry.state[kind] == 2) {
        ReleaseSRWLockExclusive(&availabilityLock);
        return false;
    }
    void* list = cache + action * actionStride;
    if (Field<unsigned>(list, 8)) {
        entry.state[kind] = 1;
        ReleaseSRWLockExclusive(&availabilityLock);
        return true;
    }
    entry.state[kind] = 2;
    const unsigned observedGeneration = generation;
    ReleaseSRWLockExclusive(&availabilityLock);

    const bool available = ProbeFrames(mob, action, list);
    AcquireSRWLockExclusive(&availabilityLock);
    const bool current = generation == observedGeneration && entry.mobTemplate == mobTemplate
        && entry.templateId == templateId;
    if (current) entry.state[kind] = available ? 1 : -1;
    ReleaseSRWLockExclusive(&availabilityLock);
    if (!available && InterlockedCompareExchange(&missingLogged[kind], 1, 0) == 0)
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=visitors_mob_climb_missing_frames template=%u action=%d fallback=air", templateId, action);
    return current && available;
}

__declspec(naked) int __fastcall OriginalDecode(void*, void*, unsigned, unsigned*) {
    __asm {
        mov edx, [esp+8]
        test edx, edx
        jmp dword ptr [decodeResume]
    }
}

__declspec(naked) unsigned __fastcall OriginalNormalize(void*, void*, unsigned) {
    __asm {
        push ebp
        mov ebp, esp
        push esi
        lea eax, [ebp+8]
        jmp dword ptr [normalizeResume]
    }
}

int __fastcall Decode(void* mob, void*, unsigned raw, unsigned* facing) {
    const int action = ClimbIndex(raw);
    if (action < 0) return OriginalDecode(mob, nullptr, raw, facing);
    if (ExtendedFrames(mob, action)) {
        if (facing) *facing = raw & 1;
        return action;
    }
    return OriginalDecode(mob, nullptr, AirRaw(raw), facing);
}

unsigned __fastcall Normalize(void* mob, void*, unsigned raw) {
    const int action = ClimbIndex(raw);
    if (action < 0) return OriginalNormalize(mob, nullptr, raw);
    if (ExtendedFrames(mob, action)) return raw;
    try {
        return OriginalNormalize(mob, nullptr, AirRaw(raw));
    } catch (...) {
        return AirRaw(raw);
    }
}

__declspec(naked) void TemplateLoop() {
    __asm {
        pushfd
        pushad
        call InvalidateAvailability
        popad
        popfd
        lea esi, actionNames
        jmp dword ptr [templateLoopResume]
    }
}

void __cdecl ReleaseNames() {
    for (int index = actionCount; index-- > 0;) releaseName(&actionNames[index]);
}

__declspec(naked) void OriginalInitialize() {
    __asm {
        mov eax, dword ptr [initializeSeh]
        jmp dword ptr [initializeResume]
    }
}

void __cdecl Activate();

__declspec(naked) void Initialize() {
    __asm {
        call OriginalInitialize
        pushfd
        pushad
        call Activate
        popad
        popfd
        ret
    }
}

struct Patch {
    DWORD address;
    const char* expected;
    size_t size;
    unsigned char replacement[8];
    DWORD protection;
};

Patch bootstrap{Native(0x004A6C05), "\xb8\xc4\x24\xa8\x00", 5, {}, 0};
Patch patches[] = {
    {Native(0x0067F4C9), "\xbe\xc8\xc4\xbe\x00", 5, {}, 0},
    {Native(0x0067F5CB), "\x81\xfe\x68\xc5\xbe\x00", 6, {0x81,0xFE}, 0},
    {Native(0x0040C901), "\x8d\x04\xb5\xc8\xc4\xbe\x00", 7, {0x8D,0x04,0xB5}, 0},
    {Native(0x00662300), "\x6a\x28", 2, {0x6A,0x2A}, 0},
    {Native(0x00664985), "\x81\xfe\x20\x03\x00\x00", 6, {0x81,0xFE,0x48,0x03,0,0}, 0},
    {Native(0x00664DAF), "\x83\xff\x28", 3, {0x83,0xFF,0x2A}, 0},
    {Native(0x00664D42), "\x8b\x54\x24\x08\x85\xd2", 6, {}, 0},
    {Native(0x00671AFF), "\x55\x8b\xec\x56\x8d\x45\x08", 7, {}, 0}
};

void SetJump(Patch& patch, const void* target) {
    std::memset(patch.replacement, 0x90, patch.size);
    patch.replacement[0] = 0xE9;
    const DWORD relative = reinterpret_cast<DWORD>(target) - patch.address - 5;
    std::memcpy(patch.replacement + 1, &relative, sizeof(relative));
}

void PreparePatches() {
    SetJump(bootstrap, Initialize);
    SetJump(patches[0], TemplateLoop);
    const DWORD table = reinterpret_cast<DWORD>(actionNames);
    const DWORD end = reinterpret_cast<DWORD>(actionNames + actionCount);
    std::memcpy(patches[1].replacement + 2, &end, sizeof(end));
    std::memcpy(patches[2].replacement + 3, &table, sizeof(table));
    SetJump(patches[6], Decode);
    SetJump(patches[7], Normalize);
}

bool Signatures(const Patch* entries, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        const auto& patch = entries[index];
        if (std::memcmp(reinterpret_cast<void*>(patch.address), patch.expected, patch.size)) {
            Failure("signature", patch.address);
            return false;
        }
    }
    return true;
}

bool RestoreProtection(Patch* entries, size_t count) {
    bool restored = true;
    while (count) {
        auto& patch = entries[--count];
        DWORD ignored;
        if (!VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size, patch.protection, &ignored)
            && !VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size, patch.protection, &ignored))
            restored = false;
    }
    return restored;
}

bool WritePatches(Patch* entries, size_t count) {
    if (!Signatures(entries, count)) return false;
    size_t acquired = 0;
    for (size_t index = 0; index < count; ++index) {
        auto& patch = entries[index];
        if (!VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size, PAGE_EXECUTE_READWRITE, &patch.protection)) {
            const bool restored = RestoreProtection(entries, acquired);
            Failure(restored ? "protect" : "protect_restore", patch.address);
            return false;
        }
        ++acquired;
    }
    for (size_t index = 0; index < count; ++index) {
        const auto& patch = entries[index];
        std::memcpy(reinterpret_cast<void*>(patch.address), patch.replacement, patch.size);
    }
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), nullptr, 0) != FALSE;
    if (!flushed) {
        for (size_t index = 0; index < count; ++index) {
            const auto& patch = entries[index];
            std::memcpy(reinterpret_cast<void*>(patch.address), patch.expected, patch.size);
        }
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        Failure("flush");
    }
    const bool restored = RestoreProtection(entries, acquired);
    if (!restored) {
        bool writable = true;
        if (flushed) {
            for (size_t index = 0; index < count; ++index) {
                DWORD ignored;
                if (!VirtualProtect(reinterpret_cast<void*>(entries[index].address), entries[index].size,
                    PAGE_EXECUTE_READWRITE, &ignored)) writable = false;
            }
            if (writable) {
                for (size_t index = 0; index < count; ++index)
                    std::memcpy(reinterpret_cast<void*>(entries[index].address), entries[index].expected, entries[index].size);
                FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
            }
        }
        const bool rollbackRestored = RestoreProtection(entries, acquired);
        Failure(writable && rollbackRestored ? "restore_rolled_back" : "restore_rollback_incomplete");
    }
    return flushed && restored;
}

void __cdecl Activate() {
    if (!armed) return;
    if (InterlockedCompareExchange(&activationAttempted, 1, 0)) return;
    if (!Signatures(patches, _countof(patches))) return;
    const auto* original = reinterpret_cast<const Name*>(Native(0x00BEC4C8));
    for (int index = 0; index < nativeActionCount; ++index) {
        if (!original[index]) {
            Failure("names_not_initialized");
            return;
        }
    }
    try {
        for (int index = 0; index < nativeActionCount; ++index) copyName(&actionNames[index], &original[index]);
        createName(&actionNames[40], L"rope");
        createName(&actionNames[41], L"ladder");
    } catch (...) {
        ReleaseNames();
        Failure("name_allocation");
        return;
    }
    if (nativeAtExit(ReleaseNames) != 0) {
        ReleaseNames();
        Failure("name_cleanup_registration");
        return;
    }
    active = WritePatches(patches, _countof(patches));
    if (!active) {
        bool originalBytes = true;
        for (const auto& patch : patches)
            if (std::memcmp(reinterpret_cast<void*>(patch.address), patch.expected, patch.size)) originalBytes = false;
        if (originalBytes) ReleaseNames();
    }
    if (active) ClientLog::Append(ClientLog::Component::Lifecycle,
        "event=visitors_mob_climb_install stage=active success=1 patches=9 actions=42");
}
}

bool VisitorsMobClimb::Install(HMODULE module) {
    if (reinterpret_cast<DWORD>(module) != Native(0x00400000)) { Failure("image_base"); return false; }
    if (armed) return true;
    const auto* base = reinterpret_cast<const unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) {
        Failure("dos_header"); return false;
    }
    const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (image->Signature != IMAGE_NT_SIGNATURE || image->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || image->FileHeader.TimeDateStamp != 0x4B7C15C9 || image->OptionalHeader.SizeOfImage != 0xA94000) {
        Failure("image_version"); return false;
    }
    if (!Signatures(&bootstrap, 1) || !Signatures(patches, _countof(patches))) return false;
    const auto* original = reinterpret_cast<const Name*>(Native(0x00BEC4C8));
    for (int index = 0; index < nativeActionCount; ++index) {
        if (original[index]) { Failure("late_install"); return false; }
    }
    PreparePatches();
    armed = WritePatches(&bootstrap, 1);
    if (armed) ClientLog::Append(ClientLog::Component::Lifecycle,
        "event=visitors_mob_climb_install stage=armed success=1 awaiting=native_names");
    return armed;
}
