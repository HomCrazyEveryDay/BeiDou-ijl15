#include <windows.h>
BOOL WINAPI TestProtect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous);
BOOL WINAPI TestFlush(HANDLE process, LPCVOID address, SIZE_T size);
#define VirtualProtect TestProtect
#define FlushInstructionCache TestFlush
#include "../ezorsia/VisitorsMobClimb.cpp"
#undef VirtualProtect
#undef FlushInstructionCache
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int assertions = 0;
int protectCalls = 0, failProtectAt = 0, failProtectCount = 1;
bool failFlush = false;
int failureLogs = 0, missingLogs = 0;
int initializeCalls = 0, createCalls = 0, failCreateAt = 0, copyCalls = 0;
bool failAtExit = false, throwFrames = false, throwFallback = false;
int frameLoads = 0, fallbackChecks = 0, clearedLists = 0;
int constructedLists = 0, destroyedLists = 0;
int selectedAction = 40;
unsigned lastTemplate = 0;
void(__cdecl* cleanup)() = nullptr;
struct FakeName { const wchar_t* text; void* narrow; LONG references; };
FakeName originalNames[40]{}, extraNames[2]{};
unsigned char mob[0x440]{}, secondMob[0x440]{}, templateA[0x50]{}, templateB[0x50]{};
bool templateAFrames[2]{true, true}, templateBFrames[2]{};
std::vector<unsigned char> disk;
unsigned char* mapped = nullptr;

template<typename Value> void Set(void* object, size_t offset, Value value) {
    *reinterpret_cast<Value*>(static_cast<unsigned char*>(object) + offset) = value;
}

void Jump(DWORD address, const void* target) {
    auto* code = reinterpret_cast<unsigned char*>(address);
    code[0] = 0xE9;
    Set(code, 1, reinterpret_cast<DWORD>(target) - address - 5);
}

void __cdecl NativeInitialize(unsigned descriptor) {
    Require(descriptor == Native(0x00A824C4), "initializer preserves SEH prologue operand");
    ++initializeCalls;
    auto* names = reinterpret_cast<Name*>(Native(0x00BEC4C8));
    for (int index = 0; index < 40; ++index) {
        originalNames[index] = {L"native", nullptr, 1};
        names[index] = &originalNames[index];
    }
    Set(reinterpret_cast<void*>(Native(0x00BEC568)), 0, 0x12345678u);
}

__declspec(naked) void InitializeTail() {
    __asm {
        push eax
        call NativeInitialize
        add esp, 4
        ret
    }
}

void* __fastcall Copy(void* destination, void*, const Name* source) {
    Require(initializeCalls > 0, "names copied only after native initialization");
    ++copyCalls;
    *static_cast<Name*>(destination) = *source;
    ++static_cast<FakeName*>(*source)->references;
    return destination;
}

void* __fastcall Create(Name* destination, void*, const wchar_t* text) {
    ++createCalls;
    if (failCreateAt == createCalls) throw 7;
    const int kind = std::wcscmp(text, L"rope") == 0 ? 0 : 1;
    Require(kind == 0 || std::wcscmp(text, L"ladder") == 0, "native wide name constructor ABI");
    extraNames[kind] = {text, nullptr, 1};
    *destination = &extraNames[kind];
    return destination;
}

void __fastcall Release(Name* name, void*) {
    if (*name) {
        Require(static_cast<FakeName*>(*name)->references > 0, "name reference has not already been released");
        --static_cast<FakeName*>(*name)->references;
        *name = nullptr;
    }
}

int __cdecl AtExitStub(void(__cdecl* callback)()) {
    if (failAtExit) return -1;
    cleanup = callback;
    return 0;
}

unsigned __cdecl ReadId(const void* encrypted, unsigned checksum) {
    Require(checksum == 0xABCD1234, "template ID checksum forwarded");
    return *static_cast<const unsigned*>(encrypted);
}

int __cdecl ReadMode(const void* encrypted, unsigned) {
    return *static_cast<const int*>(encrypted);
}

void __fastcall Frames(void*, void*, unsigned templateId, int action, void* list) {
    ++frameLoads;
    lastTemplate = templateId;
    Require(action == 40 || action == 41, "optional frames use private indices");
    if (throwFrames) {
        Set(list, 8, 3u);
        throw 9;
    }
    const bool* available = templateId == 9420075 ? templateAFrames : templateBFrames;
    Set(list, 8, available[action - 40] ? 1u : 0u);
}

void __fastcall Clear(void* list, void*) {
    ++clearedLists;
    Set(list, 8, 0u);
    Set(list, 0xC, static_cast<void*>(nullptr));
}

int __fastcall NativeFallback(void* instance, void*, int action) {
    Require(instance == mob || instance == secondMob, "native normalization receiver preserved");
    ++fallbackChecks;
    if (throwFallback) throw 11;
    return action;
}

void* __fastcall Allocate(void*, void*, size_t bytes) {
    Require(bytes == 4 + 42 * 0x14, "native allocator expands exactly 40 bytes");
    return std::calloc(1, bytes);
}

void __fastcall Free(void*, void*, void* allocation) { std::free(allocation); }

void __fastcall DestroyList(void*, void*, int deleting) {
    Require(deleting == 0, "array destructor does not individually delete list storage");
    ++destroyedLists;
}
void* listMethods[] = {reinterpret_cast<void*>(DestroyList)};

void __cdecl ConstructList(void* list) {
    ++constructedLists;
    std::memset(list, 0, actionStride);
    Set(list, 0, listMethods);
}

int __fastcall CurrentAction(void* instance, void*, void* facing) {
    Require(instance == mob && facing == nullptr, "external cache reader uses native current action");
    return selectedAction;
}

void* __fastcall CurrentFrame(void* list, void*, void* position) {
    Require(list == Field<unsigned char*>(mob, 0x434) + selectedAction * actionStride
        && position == nullptr, "external cache reader uses expanded list and original position");
    return list;
}

void InstallMocks() {
    Jump(initializeResume, InitializeTail);
    Jump(Native(0x00405210), Copy);
    Jump(Native(0x00403382), Create);
    Jump(Native(0x00403394), Release);
    Jump(Native(0x00A606AE), AtExitStub);
    Jump(Native(0x004E8152), ReadId);
    Jump(Native(0x00416563), ReadMode);
    Jump(Native(0x0040C831), Frames);
    Jump(Native(0x00416B72), Clear);
    Jump(Native(0x00671999), NativeFallback);
    Jump(Native(0x00403065), Allocate);
    Jump(Native(0x004031ED), Free);
    Jump(Native(0x00675BE8), ConstructList);
    Set(reinterpret_cast<void*>(Native(0x00671977)), 0, Native(0x00BE78D4));
    *reinterpret_cast<unsigned char*>(templateLoopResume) = 0xC3;
    *reinterpret_cast<unsigned char*>(Native(0x0040C908)) = 0xC3;
}

void SetWritable(bool writable) {
    DWORD previous;
    Require(VirtualProtect(mapped, 0xA94000, writable ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ, &previous) != FALSE,
        "set synthetic image protection");
}

void Reset() {
    SetWritable(true);
    ReleaseNames();
    std::memcpy(reinterpret_cast<void*>(bootstrap.address), bootstrap.expected, bootstrap.size);
    for (const auto& patch : patches)
        std::memcpy(reinterpret_cast<void*>(patch.address), patch.expected, patch.size);
    std::memset(reinterpret_cast<void*>(Native(0x00BEC4C8)), 0, 40 * sizeof(Name));
    armed = active = false;
    activationAttempted = failureLogged = 0;
    missingLogged[0] = missingLogged[1] = 0;
    initializeCalls = createCalls = copyCalls = 0;
    failCreateAt = failProtectAt = protectCalls = 0;
    failProtectCount = 1;
    failFlush = failAtExit = false;
    cleanup = nullptr;
    InvalidateAvailability();
}

void Unmodified() {
    for (const auto& patch : patches)
        Require(std::memcmp(reinterpret_cast<void*>(patch.address), patch.expected, patch.size) == 0,
            "no partial runtime patch after refusal");
}

void CheckProtection() {
    MEMORY_BASIC_INFORMATION region{};
    for (const auto& patch : patches) {
        Require(VirtualQuery(reinterpret_cast<void*>(patch.address), &region, sizeof(region)) != 0,
            "query patch protection");
        Require(region.Protect == PAGE_EXECUTE_READ, "original executable protection restored");
    }
}

void Start() {
    Require(VisitorsMobClimb::Install(reinterpret_cast<HMODULE>(mapped)), "bootstrap installation");
    Require(armed && !active && copyCalls == 0, "bootstrap does not prematurely copy zero names");
    Unmodified();
    reinterpret_cast<void(__cdecl*)()>(bootstrap.address)();
}

void InstallationTests() {
    Reset();
    Require(!VisitorsMobClimb::Install(nullptr), "wrong base rejected");
    Reset();
    for (size_t index = 0; index <= _countof(patches); ++index) {
        Patch& patch = index == _countof(patches) ? bootstrap : patches[index];
        auto* site = reinterpret_cast<unsigned char*>(patch.address);
        site[0] ^= 1;
        const int before = failureLogs;
        Require(!VisitorsMobClimb::Install(reinterpret_cast<HMODULE>(mapped)), "signature mismatch rejected");
        Require(!VisitorsMobClimb::Install(reinterpret_cast<HMODULE>(mapped)), "signature mismatch remains rejected");
        Require(failureLogs == before + 1, "install failure logged once");
        site[0] ^= 1;
        Unmodified();
        Reset();
    }
    Set(reinterpret_cast<void*>(Native(0x00BEC4C8)), 0, &originalNames[0]);
    Require(!VisitorsMobClimb::Install(reinterpret_cast<HMODULE>(mapped)), "late installation cannot leave live 40-slot mobs");
    Reset();
    Require(VisitorsMobClimb::Install(reinterpret_cast<HMODULE>(mapped)), "arm before delayed drift");
    *reinterpret_cast<unsigned char*>(patches[4].address) ^= 1;
    reinterpret_cast<void(__cdecl*)()>(bootstrap.address)();
    Require(!active && copyCalls == 0, "activation rechecks all bytes before copying names");
    *reinterpret_cast<unsigned char*>(patches[4].address) ^= 1;
    Unmodified();
    for (int failure : {1, 2}) {
        Reset();
        failCreateAt = failure;
        Start();
        Require(!active, "allocation exception disables extension");
        for (const auto& name : originalNames) Require(name.references == 1, "partial name copies released");
        Unmodified();
    }
    Reset();
    failAtExit = true;
    Start();
    Require(!active && extraNames[0].references == 0 && extraNames[1].references == 0, "cleanup registration failure rolls back names");
    Unmodified();
    for (int failure = 1; failure <= 8; ++failure) {
        Reset();
        Require(VisitorsMobClimb::Install(reinterpret_cast<HMODULE>(mapped)), "arm before protect failure");
        NativeInitialize(initializeSeh);
        SetWritable(false);
        protectCalls = 0;
        failProtectAt = failure;
        Activate();
        Require(!active, "failed writable acquisition rejects activation");
        Unmodified();
        CheckProtection();
    }
    Reset();
    Require(VisitorsMobClimb::Install(reinterpret_cast<HMODULE>(mapped)), "arm before flush failure");
    NativeInitialize(initializeSeh);
    SetWritable(false);
    failFlush = true;
    Activate();
    Require(!active, "failed flush rolls back patch transaction");
    Unmodified();
    CheckProtection();
    Reset();
    Require(VisitorsMobClimb::Install(reinterpret_cast<HMODULE>(mapped)), "arm before persistent restore failure");
    NativeInitialize(initializeSeh);
    SetWritable(false);
    protectCalls = 0;
    failProtectAt = 9;
    failProtectCount = 2;
    Activate();
    Require(!active, "failed protection restoration disables and rolls back runtime patches");
    Unmodified();
    CheckProtection();
    Reset();
    Require(VisitorsMobClimb::Install(reinterpret_cast<HMODULE>(mapped)), "arm final installation");
    NativeInitialize(initializeSeh);
    SetWritable(false);
    protectCalls = 0;
    failProtectAt = 9;
    Activate();
    Require(active && copyCalls == 40 && createCalls == 2 && cleanup, "all 42 native names retained");
    Require(protectCalls == 17, "transient protection restore failure is retried");
    CheckProtection();
    for (const auto& patch : patches)
        Require(std::memcmp(reinterpret_cast<void*>(patch.address), patch.replacement, patch.size) == 0,
            "installed bytes match prepared replacement");
    Require(VisitorsMobClimb::Install(reinterpret_cast<HMODULE>(mapped)), "installation idempotent");
    Require(Field<unsigned>(reinterpret_cast<void*>(Native(0x00BEC568)), 0) == 0x12345678, "adjacent action table never overwritten");
    for (int index = 0; index < 40; ++index)
        Require(actionNames[index] == &originalNames[index] && originalNames[index].references == 2,
            "native action indices and reference counts preserved");
    Require(std::wcscmp(static_cast<FakeName*>(actionNames[40])->text, L"rope") == 0
        && std::wcscmp(static_cast<FakeName*>(actionNames[41])->text, L"ladder") == 0, "private names ordered correctly");
    SetWritable(true);
    assertions += 9;
}

void LoadImage(const char* filename) {
    FILE* file = nullptr;
    Require(fopen_s(&file, filename, "rb") == 0, "open client EXE read-only");
    fseek(file, 0, SEEK_END);
    disk.resize(ftell(file));
    rewind(file);
    Require(fread(disk.data(), 1, disk.size(), file) == disk.size(), "read complete EXE");
    fclose(file);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(disk.data());
    const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS32*>(disk.data() + dos->e_lfanew);
    Require(dos->e_magic == IMAGE_DOS_SIGNATURE && image->Signature == IMAGE_NT_SIGNATURE, "PE signatures");
    Require(image->OptionalHeader.ImageBase == 0x400000 && image->OptionalHeader.SizeOfImage == 0xA94000, "supported image dimensions");
    mapped = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(Native(0x00400000)),
        image->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Require(mapped != nullptr, "map non-launching synthetic EXE");
    std::memcpy(mapped, disk.data(), image->OptionalHeader.SizeOfHeaders);
    const auto* sections = IMAGE_FIRST_SECTION(image);
    for (unsigned section = 0; section < image->FileHeader.NumberOfSections; ++section)
        std::memcpy(mapped + sections[section].VirtualAddress, disk.data() + sections[section].PointerToRawData,
            sections[section].SizeOfRawData);
    Require(Signatures(&bootstrap, 1) && Signatures(patches, _countof(patches)), "all 9 disk signatures match");
    const DWORD extraTableReferences[] = {0x40BAAF,0x40C904,0x4A61D6,0x4A6BF1,0x4A74FC,0x4A8D7C,0x67F4CA,0x67F5CD,0x67FD9C};
    const DWORD cacheReferences[] = {0x6622FC,0x664880,0x664977,0x6649B4,0x664DB6,0x666253,0x671934,0x671970,0x671984,0x7A5A64,0xAA0FF4};
    unsigned nativeNames = 0, tableHits = 0, cacheHits = 0;
    for (unsigned section = 0; section < image->FileHeader.NumberOfSections; ++section) {
        if (!(sections[section].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        for (DWORD offset = 0; offset + 4 <= sections[section].SizeOfRawData; ++offset) {
            const auto* bytes = disk.data() + sections[section].PointerToRawData + offset;
            const DWORD value = Field<DWORD>(bytes, 0);
            const DWORD address = 0x400000 + sections[section].VirtualAddress + offset;
            if (value >= 0xBEC4C8 && value <= 0xBEC568 && !(value & 3)) {
                ++tableHits;
                if (address >= 0x4A6C2E && address <= 0x4A74BE) {
                    Require(value == 0xBEC4C8 + nativeNames * 4, "native registration sequence unchanged");
                    ++nativeNames;
                } else Require(std::find(std::begin(extraTableReferences), std::end(extraTableReferences), address)
                    != std::end(extraTableReferences), "no unaudited executable name-table operand");
            }
            if (value == 0x434 && ((address >= 0x660000 && address < 0x674000)
                || address == 0x7A5A64 || address == 0xAA0FF4)) {
                ++cacheHits;
                Require(std::find(std::begin(cacheReferences), std::end(cacheReferences), address)
                    != std::end(cacheReferences), "no unaudited CMob cache displacement");
            }
        }
    }
    Require(nativeNames == 40 && tableHits == 49 && cacheHits == 11, "complete operand audit counts");
}

using DecodeFunction = int(__thiscall*)(void*, unsigned, unsigned*);
using NormalizeFunction = unsigned(__thiscall*)(void*, unsigned);
const auto decode = reinterpret_cast<DecodeFunction>(Native(0x00664D42));
const auto normalize = reinterpret_cast<NormalizeFunction>(Native(0x00671AFF));
using AllocateCache = void*(__thiscall*)(void*, unsigned, void*);
const auto allocateCache = reinterpret_cast<AllocateCache>(Native(0x00673070));
const auto releaseCache = reinterpret_cast<ClearFrames>(Native(0x00673032));
const auto clearCache = reinterpret_cast<ClearFrames>(Native(0x0066496F));

void SetUpMob(void* instance, void* mobTemplate) {
    Set(instance, 0x188, mobTemplate);
    allocateCache(static_cast<unsigned char*>(instance) + 0x434,
        *reinterpret_cast<unsigned char*>(Native(0x00662301)), nullptr);
}

void MappingTests() {
    Set(templateA, 0xC, 9420075u);
    Set(templateA, 0x14, 0xABCD1234u);
    Set(templateB, 0xC, 9420077u);
    Set(templateB, 0x14, 0xABCD1234u);
    SetUpMob(mob, templateA);
    SetUpMob(secondMob, templateA);
    Require(constructedLists == 84, "native allocation constructs all 42 lists per mob");
    for (unsigned raw = 0; raw <= 255; ++raw) {
        const int index = ClimbIndex(raw);
        if (index >= 0) {
            unsigned facing = 99;
            Require(normalize(mob, raw) == raw && decode(mob, raw, &facing) == index && facing == (raw & 1),
                "rope and ladder round trip with facing");
            Require(decode(mob, raw, nullptr) == index, "optional facing output");
        } else {
            for (int mode : {0, 3}) {
                Set(templateA, 0x40, mode);
                unsigned expectedFacing = 99, actualFacing = 99;
                const int expected = OriginalDecode(mob, nullptr, raw, &expectedFacing);
                Require(decode(mob, raw, &actualFacing) == expected && expectedFacing == actualFacing,
                    "ordinary raw decoder unchanged");
                Require(normalize(mob, raw) == OriginalNormalize(mob, nullptr, raw), "ordinary raw normalizer unchanged");
            }
        }
        ++assertions;
    }
    for (unsigned raw : {0xFFFFFFFEu,0xFFFFFFFFu,0x150u,0x80000050u}) {
        Require(ClimbIndex(raw) == -1 && decode(mob, raw, nullptr) == OriginalDecode(mob, nullptr, raw, nullptr),
            "wide raws and signed sentinels do not alias extension");
        Require(normalize(mob, raw) == OriginalNormalize(mob, nullptr, raw), "wide raw normalization untouched");
    }
    const int loads = frameLoads;
    for (int repeat = 0; repeat < 1000; ++repeat) {
        Require(normalize(mob, 0x50) == 0x50 && decode(mob, 0x53, nullptr) == 41, "cached optional frames remain playable");
    }
    Require(frameLoads == loads && loads == 2, "no repeated frame resource loading");
    Require(normalize(secondMob, 0x50) == 0x50 && frameLoads == loads + 1,
        "template cache does not skip native per-mob lazy population");
    clearedLists = 0;
    clearCache(mob);
    Require(clearedLists == 42, "native morph clear loop visits 42 entries");
    Set(mob, 0x18C, static_cast<void*>(templateB));
    Set(mob, 0x2A4, 1);
    const int beforeMissing = frameLoads;
    for (unsigned raw = 0x50; raw <= 0x53; ++raw) {
        unsigned facing = 99;
        Require(normalize(mob, raw) == AirRaw(raw) && decode(mob, raw, &facing) == 2 && facing == (raw & 1),
            "missing frames fall back to native air without losing facing");
    }
    Require(lastTemplate == 9420077 && frameLoads == beforeMissing + 2, "morph template selected and misses cached separately");
    for (int repeat = 0; repeat < 1000; ++repeat) normalize(mob, 0x52);
    Require(frameLoads == beforeMissing + 2 && missingLogs == 2, "negative cache and diagnostics bounded");
    templateBFrames[0] = true;
    InvalidateAvailability();
    Require(normalize(mob, 0x51) == 0x51, "template reload invalidates missing-frame cache");
    Require(normalize(mob, 0x53) == 7, "rope support does not imply ladder support");
    clearCache(mob);
    InvalidateAvailability();
    throwFrames = true;
    Require(normalize(mob, 0x51) == 7 && decode(mob, 0x51, nullptr) == 2, "native frame exception safely falls back");
    Require(Field<unsigned>(Field<unsigned char*>(mob, 0x434) + 40 * actionStride, 8) == 0,
        "partial frame list discarded on failure");
    throwFrames = false;
    throwFallback = true;
    Require(normalize(mob, 0x51) == 7, "optional action fallback does not propagate resource exception");
    bool normalThrew = false;
    try { normalize(mob, 6); } catch (...) { normalThrew = true; }
    Require(normalThrew, "ordinary native exceptions not hidden");
    throwFallback = false;
    Set(mob, 0x2A4, 0);
    InvalidateAvailability();
    Require(normalize(mob, 0x52) == 0x52 && lastTemplate == 9420075, "unmorph returns to real original template");
    auto* storage = Field<unsigned char*>(mob, 0x434);
    Set(storage - 4, 0, 40u);
    Require(decode(mob, 0x52, nullptr) == 2, "undersized cache is never indexed as 42");
    Set(storage - 4, 0, 42u);
}

__declspec(naked) Name* __cdecl LookupName(int) {
    __asm {
        push esi
        mov esi, [esp+8]
        mov eax, 1040c901h
        call eax
        pop esi
        ret
    }
}

__declspec(naked) Name* __cdecl RunTemplateLoop() {
    __asm {
        push esi
        mov eax, 1067f4c9h
        call eax
        mov eax, esi
        pop esi
        ret
    }
}

__declspec(naked) void __cdecl RunConstructorUnwind(void*) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 10h
        mov eax, [ebp+8]
        mov [ebp-10h], eax
        mov eax, 10aa0fefh
        call eax
        leave
        ret
    }
}

void IndexAndLifetimeTests() {
    for (int index = 0; index < actionCount; ++index)
        Require(LookupName(index) == actionNames + index, "native LEA frame-name lookup covers all 42 slots");
    const unsigned before = generation;
    Require(RunTemplateLoop() == actionNames && generation == before + 1, "template loop selects owned table and invalidates availability");
    Require(Field<DWORD>(reinterpret_cast<void*>(Native(0x0067F5CD)), 0)
        == reinterpret_cast<DWORD>(actionNames + 42), "template loop exclusive bound is 42");
    unsigned char frame[0x50]{};
    Set(frame, 0x4C, 123);
    DWORD node[6]{};
    node[5] = reinterpret_cast<DWORD>(frame);
    auto* storage = Field<unsigned char*>(mob, 0x434);
    using Duration = int(__thiscall*)(void*, int);
    const auto duration = reinterpret_cast<Duration>(Native(0x00664DA5));
    for (int action : {40, 41}) {
        Set(storage + action * actionStride, 8, 1u);
        Set(storage + action * actionStride, 0xC, node + 4);
        Require(duration(mob, action) == 123, "native duration reader accepts new indices and sums native delays");
    }
    Require(duration(mob, -1) == 0 && duration(mob, 42) == 0, "duration range still guarded");
    Jump(Native(0x00664CE0), CurrentAction);
    Jump(Native(0x007A964D), CurrentFrame);
    using QueryFrame = void*(__thiscall*)(void*);
    for (int action : {0, 39, 40, 41}) {
        selectedAction = action;
        Require(reinterpret_cast<QueryFrame>(Native(0x007A5A58))(mob) == storage + action * actionStride,
            "out-of-CMob native current-frame accessor supports private indices");
    }
    releaseCache(mob + 0x434);
    RunConstructorUnwind(secondMob);
    Require(destroyedLists == 84 && Field<void*>(mob, 0x434) == nullptr, "native count-based destructor frees all 42 lists");
    Require(Field<void*>(secondMob, 0x434) == nullptr, "constructor exception cleanup follows native count-based destructor");
    cleanup();
    for (const auto& name : originalNames) Require(name.references == 1, "exit releases copied references only");
    Require(extraNames[0].references == 0 && extraNames[1].references == 0, "exit releases native extension name objects");
}
}

BOOL WINAPI TestProtect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous) {
    ++protectCalls;
    if (failProtectAt && protectCalls >= failProtectAt && protectCalls < failProtectAt + failProtectCount) {
        SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    return VirtualProtect(address, size, protection, previous);
}

BOOL WINAPI TestFlush(HANDLE process, LPCVOID address, SIZE_T size) {
    if (failFlush) { failFlush = false; SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return FlushInstructionCache(process, address, size);
}

void ClientLog::Append(Component, const char* format, ...) {
    char text[512]{};
    va_list arguments;
    va_start(arguments, format);
    vsnprintf_s(text, sizeof(text), _TRUNCATE, format, arguments);
    va_end(arguments);
    if (std::strstr(text, "success=0 reason=")) ++failureLogs;
    if (std::strstr(text, "missing_frames")) ++missingLogs;
}

int main(int argc, char** argv) {
    Require(argc == 2, "usage: test.exe BeiDou.exe");
    LoadImage(argv[1]);
    InstallMocks();
    const unsigned char initializationFix[] = {0xE9,0x11,0x22,0x33,0x44};
    std::memcpy(reinterpret_cast<void*>(Native(0x0066B599)), initializationFix, sizeof(initializationFix));
    InstallationTests();
    MappingTests();
    IndexAndLifetimeTests();
    Require(std::memcmp(reinterpret_cast<void*>(Native(0x0066B599)), initializationFix, sizeof(initializationFix)) == 0,
        "MobInitializationFix-owned hook never touched");
    std::printf("PASS: 9 patch signatures; 49 table operands; 11 CMob cache displacements; %d signature/raw cases\n", assertions);
    std::puts("PASS: delayed native names, reference lifetime, fail-closed install, rollback and executable protection");
    std::puts("PASS: native decoder/normalizer tails, 42-slot allocation/clear/destruction/unwind, duration and name lookup");
    std::puts("PASS: per-template misses, per-mob lazy frames, morph/reload invalidation, exceptions and bounded diagnostics");
    std::puts("Native WZ/allocator/name services are mocked; no client, server, network or DLL deployment was started.");
    return 0;
}
