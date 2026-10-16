#include "stdafx.h"
#include "SoundAllocationFallback.h"
#include "ClientLog.h"
#include "detours.h"

#include <cstring>

namespace {
constexpr DWORD kTimestamp = 0x4B7C1425;
constexpr DWORD kImageSize = 0x24000;

// +AC10 test eax,eax / jne +ACA1           acmStreamSize failed
// +AC18 call +4AEE / mov eax,[eax+38h]     sound configuration
// +AC20 cmp [ebp-10h],eax / ja +ACA1       decoded size above predecode limit
// +AC25 lea ecx,[ebp-0ACh] / call +AF1D    temporary decoder (needs cleanup)
constexpr DWORD kDecisionCheckRva = 0xAC10;
const BYTE kDecisionBytes[] = {
    0x85, 0xC0, 0x0F, 0x85, 0x89, 0x00, 0x00, 0x00,
    0xE8, 0xD1, 0x9E, 0xFF, 0xFF, 0x8B, 0x40, 0x38,
    0x39, 0x45, 0xF0, 0x77, 0x7C,
    0x8D, 0x8D, 0x54, 0xFF, 0xFF, 0xFF, 0xE8, 0xED, 0x02, 0x00, 0x00
};
constexpr DWORD kDecisionPatchRva = 0xAC18;
constexpr DWORD kDecisionPatchSize = 13; // call + mov + cmp + ja
constexpr DWORD kConfigRva = 0x4AEE;
constexpr DWORD kResumeRva = 0xAC25;
constexpr DWORD kSkipRva = 0xACA1;

// +1A20 lea eax,[ebx+4] / push eax / push 0 / call [GetProcessHeap slot]
// +1A2C push eax / call [HeapAlloc slot] / +1A33 mov [eax],ebx / add eax,4
constexpr DWORD kAllocatorCheckRva = 0x1A20;
constexpr DWORD kAllocatorCallRva = 0x1A2D;
constexpr DWORD kGetHeapSlotRva = 0x1F994;
constexpr DWORD kHeapAllocSlotRva = 0x1F98C;

// +5B45 asks +19C0 for decoded+4 (array count header); the large-block path
// adds its own 4-byte size header, so HeapAlloc receives decoded+8.
constexpr DWORD kNativeHeaders = 8;
constexpr LONG kMaxLoggedFallbacks = 8;

using GetHeapFn = HANDLE(WINAPI*)();
using HeapAllocFn = LPVOID(WINAPI*)(HANDLE, DWORD, SIZE_T);

DWORD g_config = 0;
DWORD g_resume = 0;
DWORD g_skip = 0;
GetHeapFn* g_getHeapSlot = nullptr;
HeapAllocFn* g_heapAllocSlot = nullptr;
volatile LONG g_patchedBase = 0;
volatile LONG g_fallbacks = 0;
SRWLOCK g_patchLock = SRWLOCK_INIT;
decltype(&LoadLibraryA) g_originalLoadLibraryA = LoadLibraryA;

struct Pending {
    void* block = nullptr;
    HANDLE heap = nullptr;
    SIZE_T bytes = 0;
};
thread_local Pending g_pending;

void ReleaseStalePending()
{
    if (g_pending.block) {
        HeapFree(g_pending.heap, 0, g_pending.block);
        g_pending = Pending{};
    }
}

// Performs the real predecode allocation before +AC25 constructs anything.
bool __stdcall PrepareBuffer(DWORD decodedBytes)
{
    ReleaseStalePending();
    // +5B45 allocates nothing for an empty clip.
    if (decodedBytes == 0) return true;
    if (decodedBytes > MAXDWORD - kNativeHeaders) return false;
    const GetHeapFn getHeap = *g_getHeapSlot;
    const HeapAllocFn heapAlloc = *g_heapAllocSlot;
    HANDLE heap = getHeap ? getHeap() : nullptr;
    void* block = heap && heapAlloc ? heapAlloc(heap, 0, decodedBytes + kNativeHeaders) : nullptr;
    if (block) {
        g_pending = Pending{block, heap, decodedBytes + kNativeHeaders};
        return true;
    }
    const LONG count = InterlockedIncrement(&g_fallbacks);
    if (count <= kMaxLoggedFallbacks) {
        ClientLog::Emergency("sound_predecode_fallback skip decodedBytes=%lu requestBytes=%lu count=%ld",
            decodedBytes, decodedBytes + kNativeHeaders, count);
    }
    return false;
}

// Replaces +19C0's call [HeapAlloc slot]; same stdcall signature.
LPVOID WINAPI AllocatorHeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    if (g_pending.block && g_pending.bytes == bytes && g_pending.heap == heap) {
        void* block = g_pending.block;
        g_pending = Pending{};
        return block;
    }
    return (*g_heapAllocSlot)(heap, flags, bytes);
}

__declspec(naked) void PredecodeDecision()
{
    __asm {
        call dword ptr [g_config]
        mov eax, dword ptr [eax + 38h]
        cmp dword ptr [ebp - 10h], eax
        ja skip
        push dword ptr [ebp - 10h]
        call PrepareBuffer
        test al, al
        jz skip
        jmp dword ptr [g_resume]
    skip:
        jmp dword ptr [g_skip]
    }
}

bool MatchesModule(DWORD base)
{
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE
            || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
            || nt->FileHeader.TimeDateStamp != kTimestamp
            || nt->OptionalHeader.SizeOfImage != kImageSize
            || std::memcmp(reinterpret_cast<const void*>(base + kDecisionCheckRva), kDecisionBytes, sizeof(kDecisionBytes)))
            return false;
        // The slot operands are absolute and follow relocation.
        BYTE allocator[21] = {0x8D, 0x43, 0x04, 0x50, 0x6A, 0x00, 0xFF, 0x15, 0, 0, 0, 0,
            0x50, 0xFF, 0x15, 0, 0, 0, 0, 0x89, 0x18};
        const DWORD getHeapSlot = base + kGetHeapSlotRva;
        const DWORD heapAllocSlot = base + kHeapAllocSlotRva;
        std::memcpy(allocator + 8, &getHeapSlot, sizeof(getHeapSlot));
        std::memcpy(allocator + 15, &heapAllocSlot, sizeof(heapAllocSlot));
        return std::memcmp(reinterpret_cast<const void*>(base + kAllocatorCheckRva), allocator, sizeof(allocator)) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteCode(DWORD address, const BYTE* code, DWORD size)
{
    void* target = reinterpret_cast<void*>(address);
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    std::memcpy(target, code, size);
    DWORD ignored = 0;
    VirtualProtect(target, size, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    return true;
}

bool Patch(DWORD base)
{
    if (static_cast<DWORD>(InterlockedCompareExchange(&g_patchedBase, 0, 0)) == base) return true;
    AcquireSRWLockExclusive(&g_patchLock);
    bool patched = static_cast<DWORD>(g_patchedBase) == base;
    if (!patched && MatchesModule(base)) {
        g_config = base + kConfigRva;
        g_resume = base + kResumeRva;
        g_skip = base + kSkipRva;
        g_getHeapSlot = reinterpret_cast<GetHeapFn*>(base + kGetHeapSlotRva);
        g_heapAllocSlot = reinterpret_cast<HeapAllocFn*>(base + kHeapAllocSlotRva);

        // Adoption first: an installed decision always has a consumer.
        BYTE allocatorCall[6] = {0xE8, 0, 0, 0, 0, 0x90};
        const DWORD allocatorRelative = reinterpret_cast<DWORD>(&AllocatorHeapAlloc) - (base + kAllocatorCallRva + 5);
        std::memcpy(allocatorCall + 1, &allocatorRelative, sizeof(allocatorRelative));
        BYTE decision[kDecisionPatchSize];
        std::memset(decision, 0x90, sizeof(decision));
        decision[0] = 0xE9;
        const DWORD decisionRelative = reinterpret_cast<DWORD>(&PredecodeDecision) - (base + kDecisionPatchRva + 5);
        std::memcpy(decision + 1, &decisionRelative, sizeof(decisionRelative));
        patched = WriteCode(base + kAllocatorCallRva, allocatorCall, sizeof(allocatorCall))
            && WriteCode(base + kDecisionPatchRva, decision, sizeof(decision));
        if (patched) InterlockedExchange(&g_patchedBase, static_cast<LONG>(base));
    }
    ReleaseSRWLockExclusive(&g_patchLock);
    return patched;
}

void PatchIfLoaded(const char* trigger)
{
    HMODULE module = GetModuleHandleW(L"Sound_DX8.dll");
    if (!module) return;
    const DWORD base = reinterpret_cast<DWORD>(module);
    if (static_cast<DWORD>(InterlockedCompareExchange(&g_patchedBase, 0, 0)) == base) return;
    const bool patched = Patch(base);
    ClientLog::Emergency("sound_predecode_fallback patch result=%d base=%08lX trigger=%s", patched ? 1 : 0, base, trigger);
}

HMODULE WINAPI HookLoadLibraryA(LPCSTR name)
{
    HMODULE module = g_originalLoadLibraryA(name);
    // The loader lock is released here and no caller has used the module yet.
    if (module) PatchIfLoaded("load");
    return module;
}
}

bool SoundAllocationFallback::Install()
{
    PatchIfLoaded("install");
    LONG error = DetourTransactionBegin();
    if (error == NO_ERROR) {
        error = DetourUpdateThread(GetCurrentThread());
        if (error == NO_ERROR)
            error = DetourAttach(reinterpret_cast<PVOID*>(&g_originalLoadLibraryA), HookLoadLibraryA);
        if (error == NO_ERROR) error = DetourTransactionCommit();
        else DetourTransactionAbort();
    }
    ClientLog::Emergency("sound_predecode_fallback install loadHook=%d patched=%d",
        error == NO_ERROR ? 1 : 0, g_patchedBase != 0 ? 1 : 0);
    return error == NO_ERROR || g_patchedBase != 0;
}

#ifdef BEIDOU_SOUND_FALLBACK_TEST
void SoundAllocationFallback::RedirectForTest(DWORD resume, DWORD skip)
{
    g_resume = resume;
    g_skip = skip;
}

void* SoundAllocationFallback::PendingForTest()
{
    return g_pending.block;
}
#endif
