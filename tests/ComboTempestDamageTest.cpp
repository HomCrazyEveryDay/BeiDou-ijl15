// Exercise real Tempest packet dispatch, timed display, and the original EXE rendering ABI.
#include "stdafx.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
static DWORD g_time = 0;
static DWORD TestTickCount() { return g_time; }
#define GetTickCount TestTickCount
#include "DamageSyncUnderTest.h"

static void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct Position { int x, y; };
struct TestMob { unsigned char padding[0x120]; Position* position; };
static Position g_positions[] = {{101, 1000}, {202, 1000}};
static TestMob g_mobs[2];
static unsigned char g_displayer[0x200];
static bool g_lookupAvailable = true;
static int g_positionReads = 0;
static int g_spacing = 30;
struct Displayed { int damage, line, critical; DWORD time; int x, y; };
static std::vector<Displayed> g_displayed;
static void* __fastcall FindMobStub(void*, void*, int oid) {
    return g_lookupAvailable && oid >= 1 && oid <= 2 ? &g_mobs[oid - 1] : nullptr;
}
static int __fastcall GetXStub(void* ptr, void*) { ++g_positionReads; return static_cast<Position*>(ptr)->x; }
static int __fastcall GetYStub(void* ptr, void*) { ++g_positionReads; return static_cast<Position*>(ptr)->y; }
static void __fastcall ShowNumberStub(void* ptr, void*, int x, int y, int damage, int type, int critical) {
    Require(ptr == g_displayer && type == 0, "native number ABI, displayer and mob damage type");
    g_displayed.push_back({damage, (985 - y) / g_spacing, critical, g_time, x, y});
}
static void __fastcall ShowMissStub(void* ptr, void*, int x, int y, int type) {
    Require(ptr == g_displayer && type == 0, "native MISS ABI and damage type");
    g_displayed.push_back({0, (985 - y) / g_spacing, 0, g_time, x, y});
}
static void Jump(DWORD address, void* target) {
    auto* code = reinterpret_cast<unsigned char*>(address);
    code[0] = 0xE9;
    *reinterpret_cast<DWORD*>(code + 1) = reinterpret_cast<DWORD>(target) - address - 5;
    FlushInstructionCache(GetCurrentProcess(), code, 5);
}
static void LoadNativeShowDamage(const char* path) {
    FILE* file = nullptr;
    Require(fopen_s(&file, path, "rb") == 0, "open original client for native renderer comparison");
    fseek(file, 0, SEEK_END);
    std::vector<unsigned char> bytes(ftell(file));
    rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read original client");
    fclose(file);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    Require(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.ImageBase == 0x400000, "expected GMS083 image");
    const DWORD rva = 0x6691D3 - 0x400000;
    const size_t size = 0x66926E - 0x6691D3;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    bool copied = false;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (rva >= section[i].VirtualAddress && rva + size <= section[i].VirtualAddress + section[i].SizeOfRawData) {
            memcpy(reinterpret_cast<void*>(0x306691D3), bytes.data() + section[i].PointerToRawData + rva - section[i].VirtualAddress, size);
            copied = true;
            break;
        }
    }
    Require(copied, "locate complete original CMob::ShowDamage");
    auto* code = reinterpret_cast<unsigned char*>(0x306691D3);
    Require(code[0] == 0x53 && code[1] == 0x56 && code[2] == 0x57
        && code[size - 3] == 0xC2 && code[size - 2] == 0x10 && code[size - 1] == 0,
        "original ShowDamage entry and four-argument return match");
    int relocated = 0;
    for (size_t i = 0; i + 4 <= size; ++i) {
        if (*reinterpret_cast<DWORD*>(code + i) == 0x00BEBF6C) {
            *reinterpret_cast<DWORD*>(code + i) = 0x30BEBF6C;
            ++relocated;
        }
    }
    Require(relocated == 2, "relocate both native animation displayer references");
    FlushInstructionCache(GetCurrentProcess(), code, size);
}

struct CInPacket { unsigned char* Data; unsigned long DataLen, Offset, RawSeq; };
struct QueuedMobDamage { int objectId, damage; bool critical; int lineIndex; LONG sequence; };
static std::vector<QueuedMobDamage> g_queued;
static bool g_renderingServerMobDamage = false;
static LONG g_showMobDamagePacketCount = 0;
constexpr unsigned short kOpcodeShowMobDamage = 0x1001;
static unsigned short ReadUInt16LE(const unsigned char* data) { return data[0] | (data[1] << 8); }
static int ReadInt32LE(const unsigned char* data) { return ReadTempestInt(data); }
static bool ShouldPersistMobDamageSample(LONG) { return false; }
static bool QueueMobDamage(const QueuedMobDamage& damage) { g_queued.push_back(damage); return true; }
static bool ShouldSuppressBossVenomLocalDamage(void*, int) { return false; }
static void g_ShowMobDamage(void* ptr, void*, int damage, int line, int critical, int compact) {
    auto* position = static_cast<TestMob*>(ptr)->position;
    Require(position != nullptr, "native immediate draw owns a live position");
    g_displayed.push_back({damage, line, critical, g_time, position->x, position->y - 15 - line * (compact ? 15 : 30)});
}
namespace CrashReporter {
static void RecordEvent(const char*, const char*, ...) {}
static void RecordRecentEvent(const char*, const char*, ...) {}
static int CaptureHandledException(const char*, const char*, EXCEPTION_POINTERS*) {
    Require(false, "unexpected native exception"); return EXCEPTION_EXECUTE_HANDLER;
}
}
namespace EvanAttackDiagnostics { static void RenderCall(void*, int, int, int, bool) {} }
namespace HurricaneDamageSync { static bool ShouldSuppressLocalDamage(void*, int) { return false; } }
namespace SnipeDamageSync {
static bool TrackServerDamage(int, int, bool) { return false; }
static bool TryResolveLocalDamage(void*, int, int&, bool&) { return false; }
}
namespace AbsoluteDefenseSync { static bool ShouldSuppressLocalDamage(void*) { return false; } }
namespace IntegratedFinalAttack { static bool TakeAdditionalDisplayedDamage(void*, int, int, int&) { return false; } }
#include "ShadowPartnerDamageSync.h"
namespace ShadowPartnerDamageSync {
void TrackServerDamage(int, int, bool, int, int) {}
LocalResult ResolveAtNativeImpact(void*, int, int, int, int&, bool&) { return LocalResult::Unchanged; }
}
#include "DamageHooksUnderTest.h"


static void PutInt(std::vector<unsigned char>& bytes, size_t offset, int value) {
    memcpy(bytes.data() + offset, &value, sizeof(value));
}
static std::vector<unsigned char> Packet(int oid = 1) {
    std::vector<unsigned char> bytes(33);
    bytes[4] = 1; bytes[5] = 0x10;
    PutInt(bytes, 6, oid); PutInt(bytes, 10, 100);
    bytes[16] = 2; bytes[17] = 4;
    PutInt(bytes, 18, 200); bytes[22] = 1;
    PutInt(bytes, 23, 0);
    PutInt(bytes, 28, INT_MAX);
    return bytes;
}
static void Receive(std::vector<unsigned char> bytes) {
    CInPacket packet{bytes.data(), static_cast<unsigned long>(bytes.size()), 0, 0};
    Require(HandleShowMobDamagePacket(&packet), "production receive hook consumes the complete hit");
}
static void Frame(DWORD time) { g_time = time; ComboTempestDamage::Update(); }
static void ResetCase() {
    ComboTempestDamage::Reset();
    g_time = 100;
    g_positionReads = 0;
    g_lookupAvailable = true;
    g_spacing = 30;
    g_displayed.clear(); g_queued.clear();
    for (int i = 0; i < 2; ++i) g_mobs[i].position = &g_positions[i];
    for (int offset : {0x170, 0x174, 0x188, 0x18C}) *reinterpret_cast<void**>(g_displayer + offset) = g_displayer;
}
int main(int argc, char** argv) {
    Require(argc == 2, "original client path supplied");
    for (DWORD base : {0x30400000u, 0x30430000u, 0x30440000u, 0x30660000u, 0x30BE0000u}) {
        Require(VirtualAlloc(reinterpret_cast<void*>(base), 0x10000, MEM_RESERVE | MEM_COMMIT,
            PAGE_EXECUTE_READWRITE) == reinterpret_cast<void*>(base), "reserve native stubs");
    }
    *reinterpret_cast<void**>(0x30BEBFA4) = g_mobs;
    *reinterpret_cast<void**>(0x30BEBF6C) = g_displayer;
    Jump(0x30441AE8, FindMobStub); Jump(0x30403CB7, GetXStub); Jump(0x30403CDE, GetYStub);
    Jump(0x30437D0F, ShowNumberStub); Jump(0x30438A21, ShowMissStub);
    LoadNativeShowDamage(argv[1]);
    ResetCase();
    for (int row = 0; row < 4; ++row) for (int damage : {0, 1, INT_MAX}) for (bool critical : {false, true}) {
        g_displayed.clear();
        using NativeShowDamage = void (__thiscall*)(void*, int, int, int, int);
        reinterpret_cast<NativeShowDamage>(0x306691D3)(&g_mobs[0], damage, row, critical ? 1 : 0, 0);
        TempestHit hit{};
        hit.next = row; hit.lines[row] = {damage, critical};
        Require(CaptureTempestPosition(1, hit.x, hit.y) && RenderTempestLine(hit), "snapshot renders");
        Require(g_displayed.size() == 2 && g_displayed[0].damage == g_displayed[1].damage
            && g_displayed[0].critical == g_displayed[1].critical && g_displayed[0].x == g_displayed[1].x
            && g_displayed[0].y == g_displayed[1].y, "snapshot renderer matches actual EXE for critical, MISS, rows and max damage");
    }
    ResetCase(); Receive(Packet());
    Require(g_displayed.empty() && g_queued.empty(), "no drawing on receive or generic four-per-frame queue");
    Frame(100); Frame(100); Frame(189);
    Require(g_displayed.size() == 1, "four coalesced lines cannot draw in one frame or before 90ms");
    Frame(190); Frame(280); Frame(370); Frame(1000);
    Require(g_displayed.size() == 4, "one complete hit displayed exactly once");
    const int expected[] = {100, 200, 0, INT_MAX};
    for (int i = 0; i < 4; ++i) {
        Require(g_displayed[i].damage == expected[i] && g_displayed[i].line == i
            && g_displayed[i].time == 100 + i * 90, "strict line order, values and 90ms spacing");
    }
    Require(g_displayed[1].critical == 1, "critical skin retained");
    ResetCase(); Receive(Packet()); Frame(100); Frame(1500); Frame(1500); Frame(1501);
    Require(g_displayed.size() == 2, "stalled frame does not catch up all remaining lines");
    Frame(1590); Frame(1680);
    Require(g_displayed.size() == 4 && g_displayed[2].time - g_displayed[1].time == 90,
        "cadence restarts at actual render time after stall");
    ResetCase(); Receive(Packet());
    g_lookupAvailable = false; g_mobs[0].position = nullptr;
    const int reads = g_positionReads;
    Frame(100); Frame(190); Frame(280); Frame(370);
    Require(g_displayed.size() == 4 && g_positionReads == reads, "death before first frame keeps all tail lines without reading removed mob");
    ResetCase(); Receive(Packet()); Receive(Packet()); Receive(Packet(2));
    Frame(100); Frame(190); Frame(280); Frame(370);
    Require(g_displayed.size() == 12, "consecutive identical casts and multiple targets have independent timelines");
    for (int i = 0; i < 12; ++i) Require(g_displayed[i].line == i / 3, "no interleaving within a target hit");
    ResetCase(); Receive(Packet());
    *reinterpret_cast<void**>(g_displayer + 0x170) = nullptr;
    *reinterpret_cast<void**>(g_displayer + 0x188) = nullptr;
    Frame(100); Frame(1000);
    Require(g_displayed.empty(), "unready resources retain first line and block later lines");
    *reinterpret_cast<void**>(g_displayer + 0x170) = g_displayer;
    Frame(1000); Frame(1090);
    Require(g_displayed.size() == 2 && g_displayed[1].critical == 0, "recovery and critical resource fallback preserve cadence");
    ResetCase(); Receive(Packet()); ComboTempestDamage::Reset(); Frame(100);
    Require(g_displayed.empty(), "field reset drops pending hit");
    ResetCase(); Receive(Packet()); Frame(2601);
    Require(g_displayed.empty(), "expired hit cannot draw later");
    ResetCase(); g_time = 0xFFFFFFF0; Receive(Packet()); Frame(g_time); Frame(0x49);
    Require(g_displayed.size() == 1, "wraparound still enforces 90ms");
    Frame(0x4A); Require(g_displayed.size() == 2, "wraparound due time");
    ResetCase();
    auto truncated = Packet(); truncated.pop_back(); Receive(truncated);
    auto invalid = Packet(); invalid[17] = 16; Receive(invalid);
    invalid = Packet(); PutInt(invalid, 18, -1); Receive(invalid);
    invalid = Packet(); invalid[15] = 2; Receive(invalid);
    Frame(100); Require(g_displayed.empty() && g_queued.empty(), "malformed batches cannot fall into generic renderer");
    auto ordinary = Packet(); ordinary.resize(16); Receive(ordinary);
    Require(g_queued.size() == 1, "unmarked damage keeps its existing queue");
    ResetCase();
    for (int i = 0; i < 130; ++i) Receive(Packet());
    Frame(100); Require(g_displayed.size() == 128, "pending capacity remains bounded");
    std::puts("PASS: actual EXE ABI, four-line timing, batching, stall, death, critical/MISS, multiple casts/targets, retry, reset, expiry, wraparound and malformed packets");
}
