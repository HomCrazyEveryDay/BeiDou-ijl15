#include <windows.h>
#include <oleauto.h>
#include <xmmintrin.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include "../ezorsia/ClientLog.h"
#include "../ezorsia/ClientDiagnostics.h"

static std::vector<std::string> lines;
static unsigned reads = 0, batches = 0;
static bool failWriter = false;
static BOOL WINAPI CountRead(HANDLE process, LPCVOID address, LPVOID output, SIZE_T size, SIZE_T* copied) {
    ++reads; return ReadProcessMemory(process, address, output, size, copied);
}
namespace ClientLog {
EmergencyBatch::EmergencyBatch() { ++batches; }
EmergencyBatch::~EmergencyBatch() {}
void Emergency(const char* format, ...) {
    // Deliberately disturb error/FP state; the observer must restore the caller.
    SetLastError(876); _mm_setcsr(_mm_getcsr() ^ 0x2000);
    __asm { fninit
        pxor xmm0, xmm0 }
    if (failWriter) RaiseException(0xe0000042, 0, 0, nullptr);
    char text[2048]{}; va_list args; va_start(args, format);
    vsprintf_s(text, format, args); va_end(args); lines.emplace_back(text);
}
}
namespace ClientDiagnostics {
bool TrySnapshot(Snapshot& snapshot) { strcpy_s(snapshot.clientRunId, "test-run"); return true; }
}
#define ReadProcessMemory CountRead
#include "../ezorsia/ResourceLookupDiagnostics.cpp"
#undef ReadProcessMemory
using namespace ResourceLookupDiagnostics;

static void Require(bool ok, const char* why) {
    if (!ok) { std::printf("FAIL: %s lastError=%lu\n", why, GetLastError()); std::exit(1); }
}
static DWORD branch = 0, iid = 0, observed[7]{}, beforeEsp = 0, afterEsp = 0;
static DWORD invokeSite = 0, inputFrame = 0, inputOwner = 0;
static HRESULT inputHr = S_OK;
static DWORD gprEbx = 0x11223344;
static DWORD vectorSeed[4] = {0x11223344, 0xaabbccdd, 0x55667788, 0x12345678}, observedVector[4]{};
static double observedPi = 0;
__declspec(naked) static void Success() { __asm { mov branch, 1
    ret } }
__declspec(naked) static void Failed() { __asm { pop iid
    mov branch, 2
    ret } }
__declspec(naked) static void FailedQI() { __asm { mov branch, 2
    ret } }
__declspec(naked) static void Invoke() {
    __asm {
        pushad
        mov beforeEsp, esp
        mov eax, inputHr
        mov ebp, inputFrame
        mov esi, inputOwner
        mov ebx, gprEbx
        mov ecx, 0A1B2C3D4h
        mov edx, 12345678h
        mov edi, 13572468h
        fldpi
        movups xmm0, vectorSeed
        call dword ptr [invokeSite]
        movups observedVector, xmm0
        fstp qword ptr observedPi
        mov afterEsp, esp
        mov observed[0], eax
        mov observed[4], ebx
        mov observed[8], ecx
        mov observed[12], edx
        mov observed[16], esi
        mov observed[20], edi
        mov observed[24], ebp
        popad
        ret
    }
}
static BYTE storage[0x300]{}, outerStorage[0x300]{};
static DWORD frame = reinterpret_cast<DWORD>(storage + 0x180);
static DWORD outerFrame = reinterpret_cast<DWORD>(outerStorage + 0x180);
static wchar_t key[] = L"percent";
static DWORD keyPointer = reinterpret_cast<DWORD>(key);
static DWORD holder = reinterpret_cast<DWORD>(&keyPointer);
static VARIANT value{};
static void ResetReports() {
    reports = busy = 0; std::memset(seenCaller, 0, sizeof(seenCaller)); std::memset(seenKind, 0, sizeof(seenKind));
    lines.clear(); reads = batches = 0;
}
static void Prepare(DWORD caller, HRESULT hr, VARTYPE vt, DWORD pointer) {
    std::memset(storage, 0, sizeof(storage)); std::memset(outerStorage, 0, sizeof(outerStorage));
    *reinterpret_cast<DWORD*>(frame) = outerFrame;
    *reinterpret_cast<DWORD*>(frame + 4) = caller;
    *reinterpret_cast<DWORD*>(frame + 0xc) = holder;
    value = {}; value.vt = vt; value.punkVal = reinterpret_cast<IUnknown*>(pointer);
    std::memcpy(reinterpret_cast<void*>(frame - 0x20), &value, sizeof(value));
    inputFrame = frame; inputOwner = 0; inputHr = hr; gprEbx = 0x11223344; branch = iid = 0;
}
static void CheckMachine(bool qi = false) {
    const unsigned csr = _mm_getcsr(); SetLastError(177); Invoke();
    Require(GetLastError() == 177 && _mm_getcsr() == csr, "LastError and SSE control state unchanged");
    Require(beforeEsp == afterEsp, "native stack unchanged");
    Require(!std::memcmp(vectorSeed, observedVector, sizeof(vectorSeed)) && observedPi == 3.141592653589793,
        "live x87 and SSE values unchanged");
    Require(observed[0] == DWORD(inputHr) && observed[1] == (qi ? DWORD(inputHr) : gprEbx)
        && observed[2] == 0xa1b2c3d4 && observed[3] == 0x12345678 && observed[4] == inputOwner
        && observed[5] == 0x13572468 && observed[6] == frame, "native registers/results unchanged");
}
static void Jump(DWORD address, void* destination) {
    auto p = reinterpret_cast<BYTE*>(address); p[0] = 0xe9;
    *reinterpret_cast<DWORD*>(p + 1) = reinterpret_cast<DWORD>(destination) - address - 5;
}
static BYTE* Map(const wchar_t* path) {
    FILE* f = nullptr; Require(!_wfopen_s(&f, path, L"rb"), "open official EXE fixture");
    fseek(f, 0, SEEK_END); std::vector<BYTE> file(ftell(f)); rewind(f);
    Require(fread(file.data(), 1, file.size(), f) == file.size(), "read fixture"); fclose(f);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
    auto image = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(0x00400000), nt->OptionalHeader.SizeOfImage,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Require(image == reinterpret_cast<void*>(0x00400000), "isolated native image mapping");
    std::memcpy(image, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        std::memcpy(image + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData, sec[i].SizeOfRawData);
    // Never execute BeiDou.exe's entry point, imports, or gameplay code.
    return image;
}
static void VerifyInstallation(BYTE* image) {
    Require(Install(reinterpret_cast<HMODULE>(image), false), "disabled accepted");
    Require(image[0x3970] == 0x85, "disabled does not patch");
    image[0x1e545] ^= 1;
    Require(!Install(reinterpret_cast<HMODULE>(image), true), "mismatched code rejected");
    Require(image[0x3970] == 0x85 && image[0x3ae7] == 0x85, "no partial patch on mismatch");
    image[0x1e545] ^= 1;
    DWORD previous = 0;
    Require(VirtualProtect(image, 0xa94000, PAGE_EXECUTE_READ, &previous) != FALSE, "fixture uses original read/execute protection");
    Require(Install(reinterpret_cast<HMODULE>(image), true), "exact official signatures install");
    Require(Install(reinterpret_cast<HMODULE>(image), true), "install idempotent");
    for (const auto& patch : patches) {
        MEMORY_BASIC_INFORMATION memory{};
        Require(VirtualQuery(reinterpret_cast<void*>(patch.address), &memory, sizeof(memory)) == sizeof(memory)
            && memory.Protect == PAGE_EXECUTE_READ, "original page protection restored including shared page");
    }
    Require(VirtualProtect(image, 0xa94000, PAGE_EXECUTE_READWRITE, &previous) != FALSE, "prepare test-only continuations");
    Jump(propertySuccess, Success); Jump(propertyFailure, Failed);
    Jump(resourceSuccess, Success); Jump(resourceFailure, Failed);
    Jump(canvasSuccess, Success); Jump(canvasFailure, FailedQI);
    FlushInstructionCache(GetCurrentProcess(), image, 0xa94000);
}
static void VerifyLookup() {
    invokeSite = 0x00403970; ResetReports();
    for (VARTYPE vt : {VARTYPE(VT_UNKNOWN), VARTYPE(VT_DISPATCH), VARTYPE(VT_BYREF | VT_UNKNOWN)}) {
        Prepare(0x008da835, S_OK, vt, 0x1234); CheckMachine();
        Require(branch == 1 && !reports && !reads && lines.empty(), "healthy lookup has no logger/read/atomic claim");
    }
    Prepare(0x00600000, S_FALSE, VT_EMPTY, 0); CheckMachine();
    Require(!reports && !reads, "unrelated optional resource ignored");
    Prepare(0x008da835, S_FALSE, VT_EMPTY, 0); CheckMachine();
    Require(branch == 1 && reports == 1 && lines.size() == 1, "S_FALSE/empty logged, still native success branch");
    Require(lines[0].find("hr=00000001") != std::string::npos && lines[0].find("key=\"percent\"") != std::string::npos,
        "original HRESULT and live BSTR captured");
    Require(!std::memcmp(reinterpret_cast<void*>(frame - 0x20), &value, sizeof(value)), "VARIANT ownership and bytes untouched");
    CheckMachine(); Require(reports == 1 && lines.size() == 1, "same site deduplicated");
    Prepare(0x008da4ce, E_FAIL, VT_EMPTY, 0); CheckMachine();
    Require(branch == 2 && iid == 0x00bd8308 && reports == 2, "failure and property error IID preserved");
    Prepare(0x009881a0, S_OK, VT_UNKNOWN, 0); CheckMachine();
    Require(branch == 1 && reports == 3, "digit helper null object captured");
    invokeSite = 0x00403ae7;
    Prepare(0x004b7cf0, E_POINTER, VT_EMPTY, 0); CheckMachine();
    Require(branch == 2 && iid == 0x00bd8318, "resource HRESULT and error IID preserved");
    Prepare(0x008da359, S_OK, VT_EMPTY, 0); CheckMachine();
    Require(branch == 1 && lines.back().find("stage=resource") != std::string::npos, "S_OK empty parent resource captured");
    failWriter = true; Prepare(0x008da297, E_FAIL, VT_EMPTY, 0); CheckMachine(); failWriter = false;
    Require(branch == 2 && !busy, "writer failure preserves native failure and releases recursion guard");
    for (unsigned i = 0; i < 30; ++i) { Prepare(0x008da400 + i, S_FALSE, VT_EMPTY, 0); CheckMachine(); }
    Require(reports == 8, "hard eight-report session limit");
    const unsigned before = reads; CheckMachine(); Require(reads == before, "exhausted limit avoids slow observer");
}
static void VerifyQI() {
    invokeSite = 0x0041e545; ResetReports();
    Prepare(0x004b7d11, S_OK, VT_EMPTY, 0); *reinterpret_cast<DWORD*>(frame + 8) = 0x1234;
    CheckMachine(true); Require(!reports && !reads && branch == 1, "healthy QI is observation-free");
    Prepare(0x004b7d11, E_NOINTERFACE, VT_EMPTY, 0); CheckMachine(true);
    Require(branch == 2 && reports == 1 && lines[0].find("hr=80004002") != std::string::npos, "cash QI failure captured unchanged");
    Prepare(0x00428de1, S_OK, VT_EMPTY, 0);
    *reinterpret_cast<DWORD*>(frame + 0x10) = 0x008da84f;
    CheckMachine(true); Require(branch == 1 && reports == 2, "status-bar QI success with null output captured");
    Prepare(0x00428de1, E_NOINTERFACE, VT_EMPTY, 0);
    *reinterpret_cast<DWORD*>(frame + 0x10) = 0x00601234;
    const unsigned before = reads; CheckMachine(true);
    Require(reports == 2 && reads == before, "unrelated QI ignored");
}
static void VerifyDictionary() {
    ResetReports(); invokeSite = 0x00403970;
    DWORD parent[20]{}, buckets[31]{}, node[5]{}, entry[9]{};
    wchar_t nodeName[] = L"percent"; DWORD bstr = reinterpret_cast<DWORD>(nodeName);
    pcomBase = 0x50c00000; parent[0] = pcomBase + 0x131c4;
    parent[10] = reinterpret_cast<DWORD>(buckets); parent[11] = 31; parent[12] = 1;
    buckets[0] = reinterpret_cast<DWORD>(node); node[2] = reinterpret_cast<DWORD>(&bstr); node[4] = reinterpret_cast<DWORD>(entry);
    entry[8] = 0x123456; // Empty variant, with a pending lazy-load pointer.
    Prepare(0x008da835, S_OK, VT_EMPTY, 0); inputOwner = reinterpret_cast<DWORD>(parent);
    CheckMachine(); Require(lines.size() == 3 && lines[1].find("complete=1") != std::string::npos
        && lines[2].find("lazy=00123456") != std::string::npos, "empty child and lazy state captured without loading");
    Require(parent[12] == 1 && entry[8] == 0x123456, "property and lazy state not mutated");
    ResetReports(); node[1] = reinterpret_cast<DWORD>(node); CheckMachine();
    Require(lines.size() == 3 && lines[1].find("complete=0") != std::string::npos, "cyclic damaged dictionary traversal bounded");
    ResetReports(); parent[10] = 1; CheckMachine();
    Require(lines.size() == 2 && lines[1].find("complete=0") != std::string::npos, "unreadable dictionary harmless");
    ResetReports(); *reinterpret_cast<DWORD*>(frame + 0xc) = 1; CheckMachine();
    Require(branch == 1, "unreadable BSTR holder does not change result");
}
static void VerifyCriticalAndBoss() {
    ResetReports(); invokeSite = 0x00403970;
    Prepare(0x0043819d, S_FALSE, VT_EMPTY, 0); CheckMachine();
    Require(reports == 1 && branch == 1 && lines[0].find("stage=property") != std::string::npos,
        "critical effect query recorded before VARIANT cleanup without masking S_FALSE");
    invokeSite = 0x0041e545;
    Prepare(0x004381c4, E_NOINTERFACE, VT_EMPTY, 0);
    VARIANT source{}; source.vt = VT_UNKNOWN; source.punkVal = reinterpret_cast<IUnknown*>(0x11223344);
    memcpy(reinterpret_cast<void*>(outerFrame - 0x70), &source, sizeof(source));
    *reinterpret_cast<DWORD*>(outerFrame - 0x8c) = 0x11223344;
    *reinterpret_cast<DWORD*>(outerFrame - 0x18) = 0x22334455;
    CheckMachine(true);
    Require(reports == 2 && branch == 2 && lines.back().find("source=11223344") != std::string::npos
        && lines.back().find("owner=22334455") != std::string::npos && lines.back().find("vtKnown=1") != std::string::npos,
        "critical QI reads verified caller locals and keeps original failure");
    Prepare(0x005337a1, E_NOINTERFACE, VT_EMPTY, 0); CheckMachine(true);
    Require(reports == 3 && lines.back().find("vtKnown=0") != std::string::npos,
        "Boss QI does not invent a VARIANT layout");
    invokeSite = 0x00403ae7;
    Prepare(0x0053377b, S_OK, VT_EMPTY, 0); CheckMachine();
    Require(reports == 4 && branch == 1, "Boss resource empty success captured");
    for (DWORD caller : {0x005336c9u, 0x00534a1eu}) {
        Prepare(caller, S_FALSE, VT_EMPTY, 0); const unsigned before = reads; CheckMachine();
        Require(reports == 4 && reads == before, "Boss resource scope boundary respected");
    }
}
static void VerifyTruncatedName() {
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    auto memory = static_cast<BYTE*>(VirtualAlloc(nullptr, system.dwPageSize * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(memory != nullptr, "allocate damaged-key fixture");
    DWORD previous = 0;
    Require(VirtualProtect(memory + system.dwPageSize, system.dwPageSize, PAGE_NOACCESS, &previous) != FALSE,
        "protect damaged-key boundary");
    auto keyStart = reinterpret_cast<wchar_t*>(memory + system.dwPageSize - 25 * sizeof(wchar_t));
    for (unsigned i = 0; i < 25; ++i) keyStart[i] = 0x100;
    char name[160]{};
    Require(!Name(reinterpret_cast<DWORD>(keyStart), name) && name[159] == 0,
        "unreadable Unicode key near text limit truncates without CRT invalid-parameter failure");
    VirtualFree(memory, 0, MEM_RELEASE);
}
static double Time(unsigned iterations) {
    LARGE_INTEGER freq{}, a{}, b{}; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&a);
    for (unsigned i = 0; i < iterations; ++i) Invoke();
    QueryPerformanceCounter(&b); return double(b.QuadPart - a.QuadPart) * 1e9 / double(freq.QuadPart) / iterations;
}
__declspec(naked) static void Baseline() {
    __asm { test eax, eax
        jge ok
        push 00BD8308h
        jmp dword ptr [propertyFailure]
    ok: jmp dword ptr [propertySuccess] }
}
static void Benchmark() {
    ResetReports(); Prepare(0x008da835, S_OK, VT_UNKNOWN, 0x1234);
    constexpr unsigned n = 2000000;
    std::vector<double> baseline, observedTimes;
    for (int i = 0; i < 5; ++i) {
        invokeSite = reinterpret_cast<DWORD>(Baseline); baseline.push_back(Time(n));
        invokeSite = 0x00403970; observedTimes.push_back(Time(n));
    }
    std::sort(baseline.begin(), baseline.end()); std::sort(observedTimes.begin(), observedTimes.end());
    Require(!reports && !reads && lines.empty(), "ten million healthy reads produce no diagnostics or memory scans");
    std::printf("BENCH median baseline=%.2f ns observed=%.2f ns delta=%.2f ns per selected return (isolated CPU microbenchmark)\n",
        baseline[2], observedTimes[2], observedTimes[2] - baseline[2]);
}
int wmain(int argc, wchar_t** argv) {
    Require(argc == 2, "EXE fixture argument required"); auto image = Map(argv[1]);
    VerifyInstallation(image); VerifyLookup(); VerifyQI(); VerifyCriticalAndBoss(); VerifyDictionary(); VerifyTruncatedName(); Benchmark();
    std::puts("PASS: native branches/registers/stack, COM result/ownership, failure isolation, signatures, limits, healthy-path overhead");
    return 0;
}
