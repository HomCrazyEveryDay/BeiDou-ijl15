#include "stdafx.h"
#include "EvanSpReset.h"
#include "ClientLog.h"
#include <cstring>

namespace {
#ifdef EVAN_SP_RESET_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
bool installed = false;
bool IsEvan(int job) { return job == 2001 || job == 2200 || (job >= 2210 && job <= 2218); }

// GMS 084 A56BA9: ordinary scrolls are not Evan scrolls. Dedicated
// suffixes 5..9 select pairs 0..4, each with two separate growth books.
int SelectGroup(int job, int suffixMinusOne, int stage) {
    if (IsEvan(job)) {
        if (suffixMinusOne < 4 || suffixMinusOne > 8) return -1;
        const int pair = suffixMinusOne - 4;
        return stage >= pair * 2 + 1 ? 0x100 + pair : -1;
    }
    return suffixMinusOne >= 0 && suffixMinusOne < 4 && stage >= suffixMinusOne + 1 ? suffixMinusOne : -1;
}

using FormatString = void*(__cdecl*)(char**, const char*, ...);
const auto nativeFormat = reinterpret_cast<FormatString>(Native(0x00445B4B));
void Notice() {
    char message[256]{};
    WideCharToMultiByte(936, 0,
        L"\u8fd9\u5f20\u5377\u8f74\u4e0d\u9002\u7528\u4e8e\u5f53\u524d\u804c\u4e1a\u6216\u8f6c\u6570\u3002\u9f99\u795e\u8bf7\u4f7f\u7528\u5bf9\u5e94\u6210\u957f\u9636\u6bb5\u7684\u4e13\u7528\u6280\u80fd\u91cd\u7f6e\u5377\u8f74\u3002\u9053\u5177\u672a\u6d88\u8017\u3002",
        -1, message, sizeof(message), nullptr, nullptr);
    // Native formatter allocates a ZXString; Notice owns and releases its
    // by-value argument (083 992AD5). Do not free it again in the DLL.
    char* text = nullptr;
    nativeFormat(&text, "%s", message);
    using Show = int(__cdecl*)(char*, int, int, int, int);
    reinterpret_cast<Show>(Native(0x009929DD))(text, 0, 0, 0, 0);
}
int __stdcall Route(int job, int group, int stage) {
    const int result = SelectGroup(job, group, stage);
    if (result < 0) Notice();
    return result;
}
const DWORD getJob = Native(0x008D8298);
const DWORD rejected = Native(0x00A0C73D);
const DWORD evanAllocation = Native(0x00A0C7EB);
const DWORD ordinaryAllocation = Native(0x00A0C892);
__declspec(naked) void Gate() {
    __asm {
        push eax
        push esi
        mov ecx, [ebp+0ch]
        call dword ptr [getJob]
        movsx eax, ax
        push eax
        call Route
        cmp eax, -1
        je invalid
        test eax, 100h
        jz ordinary
        sub eax, 100h
        mov esi, eax
        push 0d4h
        jmp dword ptr [evanAllocation]
    ordinary:
        mov esi, eax
        push 0d4h
        jmp dword ptr [ordinaryAllocation]
    invalid:
        jmp dword ptr [rejected]
    }
}

// 084 8E8019 uses SkillEx tabs 1..10; 083 incorrectly uses the four
// ordinary Reset/SP tabs. Keep the list index (pair * 2) zero based.
const DWORD tabResume = Native(0x008B3E1E);
__declspec(naked) void TabNumber() {
    __asm {
        inc esi
        mov eax, esp
        push edi
        push edi
        push eax
        push esi
        jmp dword ptr [tabResume]
    }
}

thread_local bool evanHeading = false;
void __fastcall DrawHeading(void* dialog, void*, int stage) {
    struct Scope {
        bool previous;
        ~Scope() { evanHeading = previous; }
    } scope{evanHeading};
    // The existing target dialog stores the selected source skill at +C4.
    // Deriving the flag avoids changing its allocation size or object ABI.
    evanHeading = IsEvan(*reinterpret_cast<int*>(static_cast<char*>(dialog) + 0xC4) / 10000);
    using Draw = void(__thiscall*)(void*, int);
    reinterpret_cast<Draw>(Native(0x008B4E13))(dialog, stage);
}
void* __cdecl FormatHeading(char** output, const char* format, int stage) {
    return nativeFormat(output, evanHeading ? "UI/UIWindow.img/SkillEx/Tab/disabled/%d" : format,
        stage + (evanHeading ? 1 : 0));
}

struct Signature { DWORD address; const char* expected; size_t size; };
struct Patch { Signature signature; BYTE opcode; void* target; const char* bytes; DWORD protection; };
// Generated from the verified GMS 083 image, not searched for at runtime.
Patch patches[] = {
    {{Native(0x00486582), "\x83\xfa\x04", 3}, 0, nullptr, "\x83\xfa\x09", 0},
    {{Native(0x00A0C7C1), "\x8d\x4e\x01\x3b\xc1\x0f\x8c\x71\xff\xff\xff\x8b\x4d\x0c\xe8\xc4\xba\xec\xff\x0f\xbf\xc0\x50\xe8\xa7\x24\x00\x00\x85\xc0\x59\x68\xd4\x00\x00\x00\x0f\x84\xa7\x00\x00\x00", 42}, 0xE9, Gate, nullptr, 0},
    {{Native(0x008B3C36), "\x68\x7d\x0b\x00\x00", 5}, 0, nullptr, "\x68\x6c\x0a\x00\x00", 0},
    {{Native(0x008B3D16), "\x68\x7e\x0b\x00\x00", 5}, 0, nullptr, "\x68\x6d\x0a\x00\x00", 0},
    {{Native(0x008B3E18), "\x8b\xc4\x57\x57\x50\x56", 6}, 0xE9, TabNumber, nullptr, 0},
    {{Native(0x008B3E9D), "\x0f\x9c\xc0", 3}, 0, nullptr, "\x0f\x9e\xc0", 0},
    {{Native(0x008B3F97), "\x0f\x9c\xc0", 3}, 0, nullptr, "\x0f\x9e\xc0", 0},
    {{Native(0x008B439B), "\xe8\x73\x0a\x00\x00", 5}, 0xE8, DrawHeading, nullptr, 0},
    {{Native(0x008B5491), "\xe8\xb5\x06\xb9\xff", 5}, 0xE8, FormatHeading, nullptr, 0}
};
const Signature dependencies[] = {
    {Native(0x008B4393), "\xff\xb7\xc8\x00\x00\x00\x8b\xcf", 8},
    {Native(0x00A0C7B1), "\x4e\xe8\xe1\xba\xec\xff\x0f\xbf\xc0\x50\xe8\xa6\xc7\xad\xff\x59", 16}
};
bool Check(const Signature& signature) {
    if (std::memcmp(reinterpret_cast<void*>(signature.address), signature.expected, signature.size) == 0) return true;
    ClientLog::Append(ClientLog::Component::Lifecycle, "event=evan_sp_reset_install success=0 reason=signature address=%08lX", signature.address);
    return false;
}
void Restore(size_t count) {
    while (count) {
        auto& p = patches[--count];
        DWORD ignored;
        VirtualProtect(reinterpret_cast<void*>(p.signature.address), p.signature.size, p.protection, &ignored);
    }
}
}

bool EvanSpReset::Install(HMODULE module) {
    if (reinterpret_cast<DWORD>(module) != Native(0x00400000)) return false;
    if (installed) return true;
    auto* base = reinterpret_cast<unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || pe->FileHeader.TimeDateStamp != 0x4B7C15C9 || pe->OptionalHeader.SizeOfImage != 0xA94000) return false;
    for (const auto& p : patches) if (!Check(p.signature)) return false;
    for (const auto& s : dependencies) if (!Check(s)) return false;
    size_t count = 0;
    for (auto& p : patches) {
        if (!VirtualProtect(reinterpret_cast<void*>(p.signature.address), p.signature.size, PAGE_EXECUTE_READWRITE, &p.protection)) {
            Restore(count);
            return false;
        }
        ++count;
    }
    for (const auto& p : patches) {
        auto* site = reinterpret_cast<unsigned char*>(p.signature.address);
        if (p.opcode) {
            std::memset(site, 0x90, p.signature.size);
            site[0] = p.opcode;
            const DWORD relative = reinterpret_cast<DWORD>(p.target) - p.signature.address - 5;
            std::memcpy(site + 1, &relative, 4);
        } else std::memcpy(site, p.bytes, p.signature.size);
    }
    installed = FlushInstructionCache(GetCurrentProcess(), base, pe->OptionalHeader.SizeOfImage) != FALSE;
    if (!installed) {
        for (const auto& p : patches) std::memcpy(reinterpret_cast<void*>(p.signature.address), p.signature.expected, p.signature.size);
        FlushInstructionCache(GetCurrentProcess(), base, pe->OptionalHeader.SizeOfImage);
    }
    Restore(count);
    ClientLog::Append(ClientLog::Component::Lifecycle, "event=evan_sp_reset_install version=1 success=%d patches=9", installed);
    return installed;
}
