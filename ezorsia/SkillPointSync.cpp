#include "stdafx.h"
#include "SkillPointSync.h"
#include "SkillPointSyncState.h"
#include "ClientLog.h"
#include <cstring>

namespace {
#ifdef SKILL_POINT_SYNC_TEST
constexpr DWORD Native(DWORD address) { return address + 0x20000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
SRWLOCK stateLock = SRWLOCK_INIT;
SkillPointSync::State state;
bool installed = false;
const DWORD decodeSp = Native(0x004746DD);
const DWORD drawReturn = Native(0x008AC417);
const DWORD buttonReturn = Native(0x008AD86B);
const DWORD clickOriginal = Native(0x008AD006);
const DWORD clickAccept = Native(0x008AD227);
const DWORD clickReject = Native(0x008AD231);

int __stdcall WindowAllowance(const unsigned char* window, int original) {
    // CUISkill (not Evan's CUISkillEx): CCtrlTab at +5B8, selected tab at +3C.
    const auto tab = *reinterpret_cast<const unsigned char* const*>(window + 0x5b8);
    const int stage = tab ? *reinterpret_cast<const int*>(tab + 0x3c) : 0;
    AcquireSRWLockShared(&stateLock);
    const int result = state.ForStage(stage, static_cast<short>(original));
    ReleaseSRWLockShared(&stateLock);
    return result;
}

int __stdcall SkillAllowance(int skillId) {
    AcquireSRWLockShared(&stateLock);
    const int result = state.ForStage(SkillPointSync::Stage(skillId / 10000), -1);
    ReleaseSRWLockShared(&stateLock);
    return result;
}

__declspec(naked) void DrawAllowance() {
    __asm {
        call dword ptr [decodeSp]
        pushad
        push eax
        push esi
        call WindowAllowance
        mov [esp+1ch], eax
        popad
        jmp dword ptr [drawReturn]
    }
}
__declspec(naked) void ButtonAllowance() {
    __asm {
        call dword ptr [decodeSp]
        pushad
        push eax
        push esi
        call WindowAllowance
        mov [esp+1ch], eax
        popad
        jmp dword ptr [buttonReturn]
    }
}
__declspec(naked) void ClickAllowance() {
    __asm {
        // Preserve the native quest/unlock check before selecting the allowance.
        cmp dword ptr [ebx+2ch], 0
        jne rejected
        pushad
        push dword ptr [ebx]
        call SkillAllowance
        mov [esp+1ch], eax
        popad
        test eax, eax
        js original
        jz rejected
        jmp dword ptr [clickAccept]
    original:
        jmp dword ptr [clickOriginal]
    rejected:
        jmp dword ptr [clickReject]
    }
}

void RefreshWindow() {
    // Constructor 8AA5xx and destructor 8AAA68 own this singleton.
    auto window = *reinterpret_cast<void**>(Native(0x00BF1080));
    if (!window || *reinterpret_cast<DWORD*>(window) != Native(0x00B3B5CC)) return;
    reinterpret_cast<void(__thiscall*)(void*)>(Native(0x008AD7C9))(window);
    reinterpret_cast<void(__thiscall*)(void*, const RECT*)>(Native(0x009E04C9))(window, nullptr);
}
}

bool SkillPointSync::HandlePacket(const unsigned char* data, unsigned long size) {
    AcquireSRWLockExclusive(&stateLock);
    state.Observe(data, size);
    const bool consumed = state.Receive(data, size);
    ReleaseSRWLockExclusive(&stateLock);
    if (consumed && installed) RefreshWindow();
    return consumed;
}

void SkillPointSync::Reset() {
    AcquireSRWLockExclusive(&stateLock);
    state = {};
    ReleaseSRWLockExclusive(&stateLock);
}

bool SkillPointSync::Install() {
    struct Patch { DWORD address; unsigned char expected[10]; unsigned size; void* target; };
    const Patch patches[] = {
        {Native(0x008AC412), {0xe8,0xc6,0x82,0xbc,0xff}, 5, DrawAllowance},
        {Native(0x008AD866), {0xe8,0x72,0x6e,0xbc,0xff}, 5, ButtonAllowance},
        {Native(0x008ACFFC), {0x83,0x7b,0x2c,0,0x0f,0x85,0x2b,0x02,0,0}, 10, ClickAllowance}
    };
    for (const auto& p : patches)
        if (std::memcmp(reinterpret_cast<void*>(p.address), p.expected, p.size)) {
            ClientLog::Append(ClientLog::Component::Trace, "skill_point_sync install=signature_mismatch address=%08lX", p.address);
            return false;
        }
    // The click patch crosses a page boundary shared with the two display sites.
    // Save protection once per page so overlapping patches cannot leave it RWX.
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const DWORD pageSize = systemInfo.dwPageSize;
    struct Page { DWORD address; DWORD protection; } pages[6]{};
    unsigned pageCount = 0;
    for (const auto& p : patches) {
        const DWORD last = (p.address + p.size - 1) & ~(pageSize - 1);
        for (DWORD address = p.address & ~(pageSize - 1); address <= last; address += pageSize) {
            bool known = false;
            for (unsigned i = 0; i < pageCount; ++i) if (pages[i].address == address) known = true;
            if (known) continue;
            DWORD protection;
            if (!VirtualProtect(reinterpret_cast<void*>(address), pageSize, PAGE_EXECUTE_READWRITE, &protection)) {
                for (unsigned i = 0; i < pageCount; ++i) {
                    DWORD ignored;
                    VirtualProtect(reinterpret_cast<void*>(pages[i].address), pageSize, pages[i].protection, &ignored);
                }
                return false;
            }
            pages[pageCount++] = {address, protection};
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        const auto& p = patches[i];
        auto bytes = reinterpret_cast<unsigned char*>(p.address);
        std::memset(bytes, 0x90, p.size);
        bytes[0] = 0xe9;
        const DWORD displacement = reinterpret_cast<DWORD>(p.target) - p.address - 5;
        std::memcpy(bytes + 1, &displacement, 4);
        FlushInstructionCache(GetCurrentProcess(), bytes, p.size);
    }
    for (unsigned i = 0; i < pageCount; ++i) {
        DWORD ignored;
        VirtualProtect(reinterpret_cast<void*>(pages[i].address), pageSize, pages[i].protection, &ignored);
    }
    installed = true;
    ClientLog::Append(ClientLog::Component::Trace, "skill_point_sync install=ok version=1");
    return true;
}
