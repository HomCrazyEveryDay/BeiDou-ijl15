// Exercise the production tracker, scheduler, native rendering ABI and receive/display hooks.
#include "stdafx.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
static DWORD g_time = 0;
static DWORD TestTickCount() { return g_time; }
#define GetTickCount TestTickCount
#include "DamageSyncUnderTest.h"
namespace ComboTempestDamage { static bool HandlePacket(const unsigned char*, unsigned long) { return false; } }

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
static int ReadInt32LE(const unsigned char* data) { return ReadI32(data); }
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
#include "DamageHooksUnderTest.h"

static void PutInt(std::vector<unsigned char>& bytes, size_t offset, int value) {
    memcpy(bytes.data() + offset, &value, sizeof(value));
}
static std::vector<unsigned char> Packet(int skill, std::initializer_list<int> damage, int targets = 1) {
    const size_t stride = 22 + damage.size() * 4;
    std::vector<unsigned char> result(30 + targets * stride);
    result[0] = 0x2D;
    result[3] = static_cast<unsigned char>((targets << 4) | damage.size());
    PutInt(result, 4, skill);
    for (int target = 0; target < targets; ++target) {
        const size_t start = 30 + target * stride;
        PutInt(result, start, target + 1);
        size_t offset = start + 18;
        for (int value : damage) { PutInt(result, offset, value); offset += 4; }
    }
    return result;
}
static void Track(const std::vector<unsigned char>& packet) {
    ShadowPartnerDamageSync::TrackOutgoingAttackPacket(packet.data(), static_cast<unsigned long>(packet.size()));
}
static void Receive(int oid, int damage, bool critical, int line, bool marked = true, int pursuitLine = 0) {
    std::vector<unsigned char> bytes(marked ? 18 : 16);
    bytes[4] = 1; bytes[5] = 0x10;
    PutInt(bytes, 6, oid); PutInt(bytes, 10, damage);
    bytes[14] = critical ? 1 : 0; bytes[15] = static_cast<unsigned char>(line);
    if (marked) { bytes[16] = ShadowPartnerDamageSync::kNativeImpactMarker; bytes[17] = static_cast<unsigned char>(pursuitLine); }
    CInPacket packet{bytes.data(), static_cast<unsigned long>(bytes.size()), 0, 0};
    Require(HandleShowMobDamagePacket(&packet), "server damage packet handled");
}
static void Impact(int oid, int damage, int line, int critical = 0, int compact = 0) {
    ShowMobDamage_Hook(&g_mobs[oid - 1], nullptr, damage, line, critical, compact);
}
static void Frame(DWORD time) { g_time = time; ShadowPartnerDamageSync::Update(); }
static void Resources(bool normal, bool critical) {
    for (int offset : {0x170, 0x174}) *reinterpret_cast<int*>(g_displayer + offset) = normal ? 1 : 0;
    for (int offset : {0x188, 0x18C}) *reinterpret_cast<int*>(g_displayer + offset) = critical ? 1 : 0;
}
static void ResetCase() {
    ShadowPartnerDamageSync::Reset(); g_displayed.clear(); g_queued.clear();
    g_time = 0; g_lookupAvailable = true; g_spacing = 30; g_positionReads = 0;
    for (int i = 0; i < 2; ++i) g_mobs[i].position = &g_positions[i];
    Resources(true, true);
}
static constexpr int kTriple[] = {100, 80, 90, 50, 40, 45};
static void TripleReplies(bool pursuit) {
    for (int i = 0; i < 3; ++i) Receive(1, kTriple[i + 3], i == 1, i + 3, true, pursuit ? i + 6 : 0);
}
static void TripleImpacts(int compact = 0) {
    for (int i = 0; i < 6; ++i) { g_time = 200 + i * 120; Impact(1, kTriple[i], i, 0, compact); ShadowPartnerDamageSync::Update(); }
}
static void CheckNine(DWORD firstShadow, int spacing = 30) {
    Require(g_displayed.size() == 9 && g_queued.empty(), "exactly nine numbers; no generic frame queue");
    const int expected[] = {100, 80, 90, 50, 40, 45, 50, 40, 45};
    for (int i = 0; i < 9; ++i) {
        const auto& shown = g_displayed[i];
        Require(shown.line == i && shown.damage == expected[i], "strict 1..9 display order and exact copied values");
        Require(shown.x == 101 && shown.y == 985 - i * spacing, "native column spacing and impact anchor");
        if (i >= 3) Require(shown.time == firstShadow + (i - 3) * 120, "every shadow and pursuit separated by 120ms");
        if (i == 4 || i == 7) Require(shown.critical == 1, "original and pursuit critical preserved");
    }
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
    for (int compact : {0, 7, -1}) for (int row = 0; row < 9; ++row)
        for (int damage : {0, 1, INT_MAX}) for (bool critical : {false, true}) {
            g_spacing = compact ? 15 : 30;
            g_displayed.clear();
            using NativeShowDamage = void (__thiscall*)(void*, int, int, int, int);
            reinterpret_cast<NativeShowDamage>(0x306691D3)(&g_mobs[0], damage, row, critical ? 1 : 0, compact);
            DamageLine line{};
            line.serverDamage = damage; line.critical = critical; line.compact = compact;
            Require(ReadImpactPosition(&g_mobs[0], line) && RenderLine(line, row), "snapshot renderer accepts original inputs");
            Require(g_displayed.size() == 2 && g_displayed[0].damage == g_displayed[1].damage
                && g_displayed[0].critical == g_displayed[1].critical && g_displayed[0].x == g_displayed[1].x
                && g_displayed[0].y == g_displayed[1].y,
                "deferred rendering exactly matches original EXE calls for nine rows, both spacings, critical and MISS");
        }

    ResetCase();
    Track(Packet(4121007, {100, 80, 90, 50, 40, 45})); TripleReplies(true);
    Frame(100);
    Require(g_displayed.empty(), "authority never shows numbers before native impact");
    TripleImpacts();
    Require(g_displayed.size() == 6, "pursuits no longer draw alongside rows 4,5,6");
    Frame(919); Require(g_displayed.size() == 6, "pursuit waits a full interval after last shadow");
    Frame(920); Frame(1040); Frame(1160); CheckNine(560);
    Frame(1280); Require(g_displayed.size() == 9, "completed attack cannot repeat");

    ResetCase(); g_spacing = 15;
    Track(Packet(4121007, {100, 80, 90, 50, 40, 45})); TripleReplies(true); TripleImpacts(7);
    Frame(920); Frame(1040); Frame(1160); CheckNine(560, 15);

    ResetCase();
    Track(Packet(4121007, {100, 80, 90, 50, 40, 45})); TripleImpacts();
    Require(g_displayed.size() == 3, "late authority defers copied half only");
    g_time = 900; TripleReplies(true);
    Require(g_displayed.size() == 3 && g_queued.empty(), "late packet never enqueues pursuit before shadow");
    Frame(900); Frame(1019); Require(g_displayed.size() == 4, "late shadow interval enforced");
    for (DWORD t : {1020u, 1140u, 1260u, 1380u, 1500u}) Frame(t);
    CheckNine(900);

    ResetCase();
    Track(Packet(4121007, {100, 80, 90, 50, 40, 45})); TripleImpacts();
    g_time = 900; TripleReplies(false);
    Frame(900); Frame(901); Require(g_displayed.size() == 4, "ordinary six-hit late reply also paces its shadows");
    Frame(1020); Frame(1140);
    Require(g_displayed.size() == 6 && g_displayed[5].line == 5, "ordinary copied attack completes without pursuit");

    ResetCase();
    Track(Packet(4121007, {100, 80, 90, 50, 40, 45}));
    Receive(1, 50, false, 3, true, 6); Receive(1, 45, false, 5, true, 8); TripleImpacts();
    Require(g_displayed.size() == 4, "missing middle reply cannot be overtaken by final shadow or pursuit");
    Receive(1, 40, true, 4, true, 7); Frame(800); Frame(920); Frame(1040); Frame(1160); Frame(1280);
    Require(g_displayed.size() == 9, "mixed early and late replies retain all nine hits");
    for (int i = 0; i < 9; ++i) Require(g_displayed[i].line == i, "mixed reply order still yields consecutive rows");

    ResetCase();
    Track(Packet(4121007, {100, 80, 90, 50, 40, 45})); TripleReplies(true); TripleImpacts();
    Frame(1500); Frame(1500); Frame(1501);
    Require(g_displayed.size() == 7, "stalled frame never catches up several pursuit lines at once");
    Frame(1620); Frame(1740);
    Require(g_displayed.size() == 9 && g_displayed[7].time - g_displayed[6].time == 120,
        "remaining pursuit cadence rebases on actual draw time");

    ResetCase();
    Track(Packet(0, {100, 50})); g_time = 200; Impact(1, 50, 1, 0, 7);
    g_lookupAvailable = false; g_mobs[0].position = nullptr;
    const int reads = g_positionReads;
    Receive(1, 75, true, 1, true, 2); Frame(240); Frame(360);
    Require(g_displayed.size() == 2 && g_displayed[0].damage == 75 && g_displayed[1].damage == 75
        && g_displayed[0].y == 970 && g_displayed[1].y == 955 && g_positionReads == reads,
        "lethal late reply and pursuit retain impact position without dereferencing removed mob");
    Receive(1, 75, true, 1, true, 2); Frame(480);
    Require(g_displayed.size() == 2, "duplicate completed reply produces no extra number");

    ResetCase();
    Track(Packet(4001344, {100, 100, 50 | INT_MIN, 0}));
    Receive(1, 75, true, 2, true, 4); Receive(1, 0, false, 3, true, 5);
    g_time = 200; Impact(1, 50, 2); g_time = 320; Impact(1, 0, 3); Frame(440); Frame(560);
    Require(g_displayed.size() == 4 && g_displayed[0].damage == 75 && g_displayed[0].critical == 1
        && g_displayed[1].damage == 0 && g_displayed[2].critical == 1 && g_displayed[3].damage == 0,
        "double throw preserves critical and MISS in shadows and pursuits");

    ResetCase();
    Track(Packet(0, {80, 40})); Track(Packet(0, {80, 40}));
    Receive(1, 60, false, 1, true, 2); Receive(1, 90, true, 1, true, 2);
    g_time = 200; Impact(1, 40, 1); g_time = 260; Impact(1, 40, 1);
    Frame(320); Frame(380);
    Require(g_displayed.size() == 4 && g_displayed[0].damage == 60 && g_displayed[1].damage == 90
        && g_displayed[2].damage == 60 && g_displayed[3].damage == 90,
        "overlapping identical attacks retain their own authority and pursuit timelines");

    ResetCase();
    Track(Packet(0, {100, 50}, 2));
    for (int oid = 1; oid <= 2; ++oid) { Receive(oid, oid * 50, false, 1, true, 2); g_time = 200; Impact(oid, 50, 1); }
    Frame(320);
    Require(g_displayed.size() == 4 && g_displayed[2].x == 101 && g_displayed[3].x == 202,
        "independent targets can display simultaneously without a global frame budget");

    ResetCase();
    std::vector<unsigned char> remote(26 + 29 + 4 + 5 + 17);
    remote[4] = 0xBB; PutInt(remote, 6, 123); remote[10] = 0x16; remote[12] = 30;
    PutInt(remote, 13, 4121007); PutInt(remote, 26, 1);
    for (int i = 0; i < 6; ++i) PutInt(remote, 31 + i * 4, kTriple[i]);
    PutInt(remote, 59, 0x31504853); remote[63] = 1; PutInt(remote, 64, 1); remote[68] = 3;
    for (int i = 0; i < 3; ++i) PutInt(remote, 69 + i * 4, kTriple[i + 3]);
    Track(Packet(4121007, {100, 80, 90, 50, 40, 45}));
    ShadowPartnerDamageSync::TrackIncomingAttackPacket(remote.data(), static_cast<unsigned long>(remote.size()));
    ShadowPartnerDamageSync::BeginIncomingPacket(); ShadowPartnerDamageSync::BeginIncomingPacket();
    TripleImpacts(); ShadowPartnerDamageSync::EndIncomingPacket(); ShadowPartnerDamageSync::EndIncomingPacket();
    Require(g_displayed.size() == 6, "observer also defers pursuits until originals finish");
    Frame(920); Frame(1040); Frame(1160);
    Require(g_displayed.size() == 9, "completed observer timeline survives native packet scope");
    Receive(1, 75, true, 3); Impact(1, 50, 3);
    Require(g_displayed.size() == 10 && g_displayed.back().damage == 75, "observer cannot consume local pending attack");

    ResetCase(); remote.pop_back();
    ShadowPartnerDamageSync::TrackIncomingAttackPacket(remote.data(), static_cast<unsigned long>(remote.size()));
    ShadowPartnerDamageSync::BeginIncomingPacket(); Impact(1, 50, 3); ShadowPartnerDamageSync::EndIncomingPacket(); Frame(120);
    Require(g_displayed.size() == 1, "truncated observer extension adds no pursuit");

    ResetCase(); remote.push_back(0);
    ShadowPartnerDamageSync::TrackIncomingAttackPacket(remote.data(), static_cast<unsigned long>(remote.size()));
    ShadowPartnerDamageSync::BeginIncomingPacket(); Impact(1, 50, 3); ShadowPartnerDamageSync::EndIncomingPacket();
    ShadowPartnerDamageSync::BeginIncomingPacket(); Impact(1, 40, 4); Impact(1, 45, 5); ShadowPartnerDamageSync::EndIncomingPacket(); Frame(120);
    Require(g_displayed.size() == 3, "unmatched observer context cannot attach to a later packet");

    ResetCase();
    Track(Packet(0, {80, 40})); Receive(1, 60, true, 1, true, 2); Impact(1, 40, 1);
    Resources(false, false); Frame(120); Require(g_displayed.size() == 1, "unavailable resources retain deferred line");
    Resources(true, false); Frame(140);
    Require(g_displayed.size() == 2 && g_displayed.back().critical == 0, "critical resource failure falls back to normal digits");

    ResetCase();
    Track(Packet(0, {80, 40})); Receive(1, 60, false, 1, true, 2); Impact(1, 40, 1);
    ShadowPartnerDamageSync::Reset(); Frame(120); Receive(1, 60, false, 1, true, 2); Frame(240);
    Require(g_displayed.size() == 1, "map reset drops delayed rows and stale replies");
    ResetCase(); Track(Packet(0, {80, 40})); Receive(1, 60, false, 1, true, 2); Impact(1, 40, 1);
    Frame(2501); Require(g_displayed.size() == 1, "expired pursuit cannot appear later");

    ResetCase(); g_time = 0xFFFFFFF0u;
    Track(Packet(0, {80, 40})); Receive(1, 60, false, 1, true, 2); Impact(1, 40, 1);
    Frame(0x67); Require(g_displayed.size() == 1, "tick rollover does not release early");
    Frame(0x68); Require(g_displayed.size() == 2, "unsigned elapsed time survives tick rollover");

    ResetCase();
    Track(Packet(0, {80, 40})); Receive(1, 999, true, 1, false);
    Require(g_queued.size() == 1, "unmarked display packets retain generic queue routing");
    for (int skill : {4121003, 4121004, 14001004, 3121004}) {
        ResetCase(); Track(Packet(skill, {100, 50})); Impact(1, 50, 1); Frame(120);
        Require(g_displayed.size() == 1 && g_displayed[0].damage == 50, "unrelated skills remain native");
    }
    for (int skill : {0, 4101005, 4111004, 4111005, 4121008}) {
        ResetCase(); Track(Packet(skill, {1, 1})); Receive(1, 1, false, 1, true, 2);
        Impact(1, 1, 0); Impact(1, 1, 1); Frame(120);
        Require(g_displayed.size() == 3, "all supported one-line throwing skills retain immune hits and pursuit");
        Track(Packet(skill, {1})); Impact(1, 1, 0);
        Require(g_displayed.size() == 4, "non-copied packet remains native");
    }
    ResetCase(); auto truncated = Packet(4121007, {100, 80, 90, 50, 40, 45}); truncated.pop_back();
    Track(truncated); Impact(1, 50, 3);
    Require(g_displayed.size() == 1, "truncated outgoing packets cannot suppress native damage");
    std::puts("PASS: ordered 6/9-line timing, late/mixed replies, stalled frames, native spacing/ABI, lethal snapshots, critical/MISS, overlapping attacks, targets, observer isolation, resources, expiry/reset and tick rollover");
}
