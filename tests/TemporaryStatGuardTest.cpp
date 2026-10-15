#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../ezorsia/TemporaryStatGuard.h"

namespace CrashReporter { void RecordEvent(const char*, const char*, ...) {} }
constexpr DWORD Relocation = 0x30000000;
static DWORD N(DWORD address) { return address + Relocation; }
static void Require(bool ok, const char* message) {
    if (!ok) { printf("FAIL %s\n", message); exit(1); }
}
static BYTE* Map(const char* path) {
    FILE* file = nullptr; Require(!fopen_s(&file, path, "rb"), "open client image");
    fseek(file, 0, SEEK_END); std::vector<BYTE> bytes(ftell(file)); rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read client image"); fclose(file);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    auto base = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(N(0x400000)), nt->OptionalHeader.SizeOfImage,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Require(base != nullptr, "map without executing game entry point");
    memcpy(base, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(base + sec[i].VirtualAddress, bytes.data() + sec[i].PointerToRawData, sec[i].SizeOfRawData);
    return base;
}
static void Jump(DWORD from, void* to) {
    auto p = reinterpret_cast<BYTE*>(N(from)); p[0] = 0xe9;
    DWORD rel = reinterpret_cast<DWORD>(to) - reinterpret_cast<DWORD>(p) - 5;
    memcpy(p + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}

// Exact 51-byte packet captured in logs(15), including the four transport bytes.
static const BYTE hyperBody[] = {
    0xbd,0xa0,0x92,0xa0,0x20,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x60,0,0,
    0x3c,0,0x0f,0xda,0x13,0,0xe0,0x93,4,0,0x3c,0,0x0f,0xda,0x13,0,
    0xe0,0x93,4,0,0,0,0,0,0,0x3c,0,0,0
};
static const BYTE cancellation[] = {0,0,0,0,0x21,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x60,0,0,0};
struct Packet { DWORD padding[2]; const BYTE* bytes; DWORD size, unknown, cursor; };
static BYTE context[0x4000];
static DWORD user[16];
static int gives, cancels;
static bool throwHandler;
static void CheckInput(void* self, void* value, const BYTE* expected, unsigned size) {
    auto p = static_cast<Packet*>(value);
    Require(self == context && p->cursor == 6 && p->size == size, "native handler ABI/cursor unchanged");
    Require(!memcmp(p->bytes, expected, size), "captured packet reaches native handler unchanged");
    Require(*reinterpret_cast<void**>(N(0xbebf98)) == user, "native handler requires a live local user");
    if (throwHandler) RaiseException(0xe0424242, 0, 0, nullptr);
    p->cursor = size;
}
// The dispatcher, jump table, call sites and return cleanup are native EXE code.
// Business handlers below are probes, not a simulated game/SecondaryStat decoder.
static void __fastcall GiveProbe(void* self, void*, void* input) {
    ++gives; CheckInput(self, input, hyperBody, sizeof(hyperBody));
}
static void __fastcall CancelProbe(void* self, void*, void* input) {
    ++cancels; CheckInput(self, input, cancellation, sizeof(cancellation));
}
static bool Dispatch(unsigned opcode, Packet* input) {
    using Handler = void(__thiscall*)(void*, unsigned, Packet*);
    __try { reinterpret_cast<Handler>(N(0xa07a08))(context, opcode, input); }
    __except(GetExceptionCode() == 0xe0424242 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
    return true;
}
static DWORD faultSite;
static DWORD faultAddress;
__declspec(naked) static void InvokeFaultFragment() {
    __asm {
        push esi
        push edi
        push ebx
        call dword ptr [faultSite]
        pop ebx
        pop edi
        pop esi
        ret
    }
}
static int Capture(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    faultAddress = reinterpret_cast<DWORD>(info->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}
static bool Faults() {
    __try { InvokeFaultFragment(); }
    __except(Capture(GetExceptionInformation())) { return true; }
    return false;
}

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Require(argc == 2, "client path"); auto base = Map(argv[1]);
    Require(!TemporaryStatGuard::Install(nullptr), "reject absent image");
    for (DWORD offset : {0x607a4b, 0x607a58, 0x620581, 0x620805, 0x138c9e}) {
        base[offset] ^= 1;
        Require(!TemporaryStatGuard::Install(reinterpret_cast<HMODULE>(base)), "reject changed native signatures");
        base[offset] ^= 1;
    }
    Require(TemporaryStatGuard::Install(reinterpret_cast<HMODULE>(base)), "install both guarded calls");
    Require(TemporaryStatGuard::Install(reinterpret_cast<HMODULE>(base)), "installation idempotent");

    // Reproduce both original faults in their exact native getter/dereference
    // fragments. Only absolute relocation and the post-dereference exit change.
    *reinterpret_cast<DWORD*>(N(0x538c9f)) = N(0xbebf98);
    *reinterpret_cast<BYTE*>(N(0xa20588)) = 0xc3;
    const BYTE cancelExit[] = {0x83,0xc4,0x18,0xc3};
    memcpy(reinterpret_cast<void*>(N(0xa20809)), cancelExit, sizeof(cancelExit));
    faultSite = N(0xa20581);
    Require(Faults() && faultAddress == N(0xa20586), "baseline give reads null at original crash instruction");
    faultSite = N(0xa207f9);
    Require(Faults() && faultAddress == N(0xa20807), "baseline cancel reads null at matching lifecycle fault");
    *reinterpret_cast<void**>(N(0xbebf98)) = user;
    Require(!Faults(), "native cancel dereference works with local user");
    faultSite = N(0xa20581); Require(!Faults(), "native give dereference works with local user");

    *reinterpret_cast<DWORD*>(N(0xa07a1d)) = N(0xa07e8e);
    *reinterpret_cast<DWORD*>(N(0xa07e8e) + 3 * 4) = N(0xa07a48);
    *reinterpret_cast<DWORD*>(N(0xa07e8e) + 4 * 4) = N(0xa07a55);
    Jump(0xa202be, GiveProbe); Jump(0xa2071f, CancelProbe);
    *reinterpret_cast<void**>(context + 0x20b8) = user; // CharacterData survives in the cash shop.
    for (int i = 0; i < 512; ++i) {
        *reinterpret_cast<void**>(N(0xbebf98)) = nullptr;
        Packet givePacket = {{}, hyperBody, sizeof(hyperBody), 0, 6};
        Packet cancelPacket = {{}, cancellation, sizeof(cancellation), 0, 6};
        Require(Dispatch(0x20, &givePacket) && Dispatch(0x21, &cancelPacket), "missing local user safe");
        Require(gives == i && cancels == i && givePacket.cursor == 6 && cancelPacket.cursor == 6,
            "no absent-field mutation or queued replay");
        *reinterpret_cast<void**>(N(0xbebf98)) = user;
        Require(Dispatch(0x20, &givePacket) && Dispatch(0x21, &cancelPacket), "restored field routes both packets");
        Require(gives == i + 1 && cancels == i + 1 && givePacket.cursor == sizeof(hyperBody)
            && cancelPacket.cursor == sizeof(cancellation), "native path and independent packet boundaries preserved");
    }
    throwHandler = true;
    Packet givePacket = {{}, hyperBody, sizeof(hyperBody), 0, 6};
    Packet cancelPacket = {{}, cancellation, sizeof(cancellation), 0, 6};
    Require(!Dispatch(0x20, &givePacket) && !Dispatch(0x21, &cancelPacket), "live-handler exceptions propagate");
    puts("PASS: two original native AV fragments; exact dump packet through native dispatcher; 512 shop/field cycles; ABI, signatures, exception propagation");
}
