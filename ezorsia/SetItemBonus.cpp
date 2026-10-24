#include "stdafx.h"
#include "SetItemBonus.h"
#include "SetItemTooltip.h"
#include "ClientLog.h"
#include <climits>
#include <cstring>

namespace {
#ifdef SET_ITEM_BONUS_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
struct Snapshot {
    unsigned pieces;
    int values[4];
};
struct State {
    Snapshot snapshot;
    void* context;
    void* character;
    bool pending;
};
State state{};
SRWLOCK stateLock = SRWLOCK_INIT;
volatile LONG gameThread = 0;
LONG failureLogged = 0, invalidLogged = 0;
bool installed = false, refreshing = false;
const DWORD statResume = Native(0x0077F875);
const size_t offsets[] = {0x30, 0x90, 0xC0, 0xF0};
using ReadSecureInt = int(__cdecl*)(const void*, unsigned);
using WriteSecureInt = unsigned(__fastcall*)(int, void*);
using ValidateStat = void(__thiscall*)(void*);

void* Context() { return *reinterpret_cast<void**>(Native(0x00BE7918)); }
void* Character(void* context) {
    return context ? *reinterpret_cast<void**>(static_cast<unsigned char*>(context) + 0x20B8) : nullptr;
}

bool Decode(const unsigned char* data, unsigned long size, Snapshot& result) {
    result = {};
    if (!data || size != 16 || data[6] != 1 || data[7] > 4) return false;
    Snapshot decoded{};
    decoded.pieces = data[7];
    for (size_t index = 0; index < 4; ++index) {
        const unsigned value = data[8 + index * 2] | (data[9 + index * 2] << 8);
        if (value > 0x7FFF) return false;
        decoded.values[index] = static_cast<int>(value);
    }
    result = decoded;
    return true;
}

void __stdcall ApplyBonus(void* secondary) {
    void* context = Context();
    void* character = Character(context);
    if (!character || secondary != static_cast<unsigned char*>(context) + 0x2134) return;
    const LONG thread = static_cast<LONG>(GetCurrentThreadId());
    InterlockedCompareExchange(&gameThread, thread, 0);
    if (InterlockedCompareExchange(&gameThread, 0, 0) != thread) return;
    AcquireSRWLockExclusive(&stateLock);
    if (state.context && (state.context != context || state.character != character)) state = {};
    if (!state.context && state.pending) {
        state.context = context;
        state.character = character;
    }
    const Snapshot snapshot = state.snapshot;
    ReleaseSRWLockExclusive(&stateLock);
    const auto read = reinterpret_cast<ReadSecureInt>(Native(0x00416563));
    const auto write = reinterpret_cast<WriteSecureInt>(Native(0x004165B1));
    for (size_t index = 0; index < 4; ++index) {
        if (!snapshot.values[index]) continue;
        auto* encoded = static_cast<unsigned char*>(secondary) + offsets[index];
        auto* checksum = reinterpret_cast<unsigned*>(encoded + 8);
        const int original = read(encoded, *checksum);
        const int bonus = snapshot.values[index];
        *checksum = write(original > INT_MAX - bonus ? INT_MAX : original + bonus, encoded);
    }
}

__declspec(naked) void BeforeEquipment() {
    __asm {
        pushfd
        pushad
        push esi
        call ApplyBonus
        popad
        popfd
        mov [ebp-28h], eax
        or eax, -1
        jmp dword ptr [statResume]
    }
}

void RefreshPending() {
    if (!installed || InterlockedCompareExchange(&gameThread, 0, 0) != static_cast<LONG>(GetCurrentThreadId())
        || refreshing) return;
    void* context = Context();
    void* character = Character(context);
    if (!character) return;
    AcquireSRWLockExclusive(&stateLock);
    if (state.context && (state.context != context || state.character != character)) state = {};
    const bool pending = state.pending;
    if (pending) {
        state.context = context;
        state.character = character;
        state.pending = false;
    }
    ReleaseSRWLockExclusive(&stateLock);
    if (!pending) return;
    refreshing = true;
    __try {
        reinterpret_cast<ValidateStat>(Native(0x00A0843C))(context);
    } __finally {
        refreshing = false;
    }
    SetItemTooltip::Refresh();
}

struct Signature { DWORD address; const char* expected; size_t size; };
const Signature signatures[] = {
    {Native(0x0077F86F), "\x89\x45\xd8\x83\xc8\xff", 6},
    {Native(0x00416563), "\x55\x8b\xec\x8b\x45\x08\x53\x8b\x58\x04", 10},
    {Native(0x004165B1), "\x53\x56\x8b\xd9\x57\xb9\x38\xdd\xbe\x00\x8b\xfa", 12},
    {Native(0x00A0843C), "\xb8\xf7\x9a\xae\x00\xe8\x52\x87\x05\x00", 10}
};

bool Failure(const char* reason, DWORD address) {
    if (InterlockedCompareExchange(&failureLogged, 1, 0) == 0)
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=set_item_bonus_install success=0 reason=%s address=%08lX", reason, address);
    return false;
}
}

bool SetItemBonus::HandlePacket(const unsigned char* data, unsigned long size) {
    if (!data || size < 6) return false;
    const unsigned opcode = data[4] | (data[5] << 8);
    if (opcode == 0) {
        AcquireSRWLockExclusive(&stateLock);
        state = {};
        ReleaseSRWLockExclusive(&stateLock);
        return false;
    }
    if (opcode != 0x1011) {
        RefreshPending();
        return false;
    }
    Snapshot snapshot{};
    if (!Decode(data, size, snapshot) && InterlockedCompareExchange(&invalidLogged, 1, 0) == 0)
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=set_item_bonus_packet rejected=1 size=%lu", size);
    AcquireSRWLockExclusive(&stateLock);
    state = {snapshot, nullptr, nullptr, true};
    ReleaseSRWLockExclusive(&stateLock);
    RefreshPending();
    return true;
}

bool SetItemBonus::Install(HMODULE module) {
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
    for (const auto& signature : signatures)
        if (std::memcmp(reinterpret_cast<void*>(signature.address), signature.expected, signature.size))
            return Failure("signature", signature.address);
    const auto& patch = signatures[0];
    auto* site = reinterpret_cast<unsigned char*>(patch.address);
    DWORD protection;
    if (!VirtualProtect(site, patch.size, PAGE_EXECUTE_READWRITE, &protection))
        return Failure("protect", patch.address);
    site[0] = 0xE9;
    const DWORD displacement = reinterpret_cast<DWORD>(BeforeEquipment) - patch.address - 5;
    std::memcpy(site + 1, &displacement, 4);
    site[5] = 0x90;
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, patch.size) != FALSE;
    if (!flushed) {
        std::memcpy(site, patch.expected, patch.size);
        FlushInstructionCache(GetCurrentProcess(), site, patch.size);
    }
    DWORD ignored;
    const bool restored = VirtualProtect(site, patch.size, protection, &ignored) != FALSE;
    installed = flushed;
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "event=set_item_bonus_install version=1 success=%d protectionRestored=%d", installed, restored);
    return installed;
}
