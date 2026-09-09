// Exercise the production tracker and both production receive/display hooks.
#include "DamageSyncUnderTest.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static char g_mobs[2];
static bool g_lookupAvailable = true;
static int g_time = 0;
static void* __fastcall FindMobStub(void*, void*, int oid) {
    return g_lookupAvailable && oid >= 1 && oid <= 2 ? &g_mobs[oid - 1] : nullptr;
}
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
        for (int value : damage) {
            PutInt(result, offset, value);
            offset += 4;
        }
    }
    return result;
}
static void Track(const std::vector<unsigned char>& packet) {
    ShadowPartnerDamageSync::TrackOutgoingAttackPacket(packet.data(), static_cast<unsigned long>(packet.size()));
}

struct CInPacket { unsigned char* Data; unsigned long DataLen, Offset, RawSeq; };
struct QueuedMobDamage { int objectId, damage; bool critical; int lineIndex; LONG sequence; };
struct Displayed { void* mob; int damage, line, critical, compact, time; };
static std::vector<QueuedMobDamage> g_queued;
static std::vector<Displayed> g_displayed;
static bool g_renderingServerMobDamage = false;
static LONG g_showMobDamagePacketCount = 0;
constexpr unsigned short kOpcodeShowMobDamage = 0x1001;
static unsigned short ReadUInt16LE(const unsigned char* data) { return data[0] | (data[1] << 8); }
static int ReadInt32LE(const unsigned char* data) { return ReadI32(data); }
static bool ShouldPersistMobDamageSample(LONG) { return false; }
static bool QueueMobDamage(const QueuedMobDamage& damage) { g_queued.push_back(damage); return true; }
static bool ShouldSuppressBossVenomLocalDamage(void*, int) { return false; }
static void g_ShowMobDamage(void* mob, void*, int damage, int line, int critical, int compact) {
    g_displayed.push_back({mob, damage, line, critical, compact, g_time});
}
namespace CrashReporter {
static void RecordEvent(const char*, const char*, ...) {}
static void RecordRecentEvent(const char*, const char*, ...) {}
static int CaptureHandledException(const char*, const char*, EXCEPTION_POINTERS*) {
    Require(false, "unexpected native exception");
    return EXCEPTION_EXECUTE_HANDLER;
}
}
namespace HurricaneDamageSync { static bool ShouldSuppressLocalDamage(void*, int) { return false; } }
namespace SnipeDamageSync {
static bool TrackServerDamage(int, int, bool) { return false; }
static bool TryResolveLocalDamage(void*, int, int&, bool&) { return false; }
}
namespace AbsoluteDefenseSync { static bool ShouldSuppressLocalDamage(void*) { return false; } }
namespace IntegratedFinalAttack {
static bool TakeAdditionalDisplayedDamage(void*, int, int, int&) { return false; }
}
#include "DamageHooksUnderTest.h"

static void Receive(int oid, int damage, bool critical, int line, bool marked = true, int pursuitLine = 0) {
    std::vector<unsigned char> bytes(marked ? 18 : 16);
    bytes[4] = 1;
    bytes[5] = 0x10;
    PutInt(bytes, 6, oid);
    PutInt(bytes, 10, damage);
    bytes[14] = critical ? 1 : 0;
    bytes[15] = static_cast<unsigned char>(line);
    if (marked) {
        bytes[16] = ShadowPartnerDamageSync::kNativeImpactMarker;
        bytes[17] = static_cast<unsigned char>(pursuitLine);
    }
    CInPacket packet{bytes.data(), static_cast<unsigned long>(bytes.size()), 0, 0};
    Require(HandleShowMobDamagePacket(&packet), "server damage packet handled");
}
static void Impact(int oid, int damage, int line, int critical = 0, int compact = 0) {
    ShowMobDamage_Hook(&g_mobs[oid - 1], nullptr, damage, line, critical, compact);
}
static void ResetCase() {
    ShadowPartnerDamageSync::Reset();
    g_displayed.clear();
    g_queued.clear();
    g_time = 0;
    g_lookupAvailable = true;
}

int main() {
    for (DWORD base : {0x30440000u, 0x30BE0000u}) {
        Require(VirtualAlloc(reinterpret_cast<void*>(base), 0x10000,
            MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE) == reinterpret_cast<void*>(base), "reserve native stubs");
    }
    *reinterpret_cast<void**>(0x30BEBFA4) = g_mobs;
    auto* code = reinterpret_cast<unsigned char*>(0x30441AE8);
    code[0] = 0xE9;
    *reinterpret_cast<DWORD*>(code + 1) = reinterpret_cast<DWORD>(FindMobStub) - 0x30441AE8 - 5;
    FlushInstructionCache(GetCurrentProcess(), code, 5);

    Track(Packet(4121007, {100, 80, 90, 50, 40, 45}));
    for (int i = 0; i < 3; ++i) Receive(1, std::vector<int>{50, 40, 45}[i], i == 1, i + 3, true, i + 6);
    Require(g_displayed.empty() && g_queued.empty(), "nine-line stack still waits for native impact");
    for (int i = 0; i < 6; ++i) Impact(1, std::vector<int>{100, 80, 90, 50, 40, 45}[i], i, 0, 7);
    Require(g_displayed.size() == 9 && g_queued.empty(), "six native lines plus three pursuits without another attack or queue");
    int stack[9] = {};
    for (const auto& shown : g_displayed) {
        Require(shown.line >= 0 && shown.line < 9 && shown.compact == 7, "pursuit preserves native stack spacing");
        stack[shown.line] = shown.damage;
        if (shown.line == 4 || shown.line == 7) Require(shown.critical == 1, "pursuit retains shadow critical flag");
    }
    const int expectedStack[] = {100, 80, 90, 50, 40, 45, 50, 40, 45};
    for (int i = 0; i < 9; ++i) Require(stack[i] == expectedStack[i], "nine distinct rows carry exact requested values");

    ResetCase();
    Track(Packet(0, {100, 50}));
    Impact(1, 50, 1);
    Receive(1, 50, true, 1, true, 2);
    Require(g_queued.size() == 2, "late authority releases both shadow and pursuit after impact");
    Receive(1, 50, true, 1, true, 2);
    Require(g_queued.size() == 2, "late duplicate cannot repeat pursuit");

    ResetCase();
    // Server RANGED_ATTACK: six original lines, then an SHP1 pursuit trailer.
    std::vector<unsigned char> remote(26 + 29 + 4 + 5 + 17);
    remote[4] = 0xBB;
    PutInt(remote, 6, 123);
    remote[10] = 0x16;
    remote[11] = 0x5B;
    remote[12] = 30;
    PutInt(remote, 13, 4121007);
    PutInt(remote, 26, 1);
    for (int i = 0; i < 6; ++i) PutInt(remote, 31 + i * 4, expectedStack[i]);
    PutInt(remote, 59, 0x31504853);
    remote[63] = 1;
    PutInt(remote, 64, 1);
    remote[68] = 3;
    for (int i = 0; i < 3; ++i) PutInt(remote, 69 + i * 4, expectedStack[i + 6]);
    Track(Packet(4121007, {100, 80, 90, 50, 40, 45}));
    ShadowPartnerDamageSync::TrackIncomingAttackPacket(remote.data(), static_cast<unsigned long>(remote.size()));
    Require(g_displayed.empty(), "observer trailer does not display before native processing");
    ShadowPartnerDamageSync::BeginIncomingPacket();
    for (int i = 0; i < 6; ++i) Impact(1, expectedStack[i], i);
    ShadowPartnerDamageSync::EndIncomingPacket();
    Require(g_displayed.size() == 9, "observer sees same nine-line native stack");
    Receive(1, 50, false, 3, true, 6);
    Impact(1, 50, 3);
    Require(g_displayed.size() == 11, "observer pursuit cannot consume local pending attack");

    ResetCase();
    remote.pop_back();
    ShadowPartnerDamageSync::TrackIncomingAttackPacket(remote.data(), static_cast<unsigned long>(remote.size()));
    ShadowPartnerDamageSync::BeginIncomingPacket();
    Impact(1, 50, 3);
    ShadowPartnerDamageSync::EndIncomingPacket();
    Require(g_displayed.size() == 1, "truncated observer trailer cannot create extra hits");

    ResetCase();

    Track(Packet(4121007, {80, 80, 80, 40, 40, 40}, 2));
    g_time = 20;
    for (int oid = 1; oid <= 2; ++oid) {
        for (int line = 3; line < 6; ++line) Receive(oid, 60, false, line);
    }
    Require(g_queued.empty() && g_displayed.empty(), "early replies never queue or display before projectile impact");
    g_time = 200;
    Impact(1, 80, 0, 0, 7);
    Require(g_displayed.size() == 1 && g_displayed.back().damage == 80
        && g_displayed.back().time == 200 && g_displayed.back().compact == 7, "native body timing and parameters are unchanged");
    for (int oid = 1; oid <= 2; ++oid) {
        for (int line = 3; line < 6; ++line) {
            g_time += 40;
            const size_t count = g_displayed.size();
            Impact(oid, 40, line, 0, 7);
            Require(g_displayed.size() == count + 1 && g_displayed.back().damage == 60
                && g_displayed.back().line == line && g_displayed.back().time == g_time
                && g_displayed.back().compact == 7, "exactly one amplified number at each native copied impact");
        }
    }
    Require(g_queued.empty(), "normal synchronized hits never enter immediate server display queue");

    ResetCase();
    Track(Packet(0, {100, 50}));
    g_time = 200;
    Impact(1, 50, 1);
    Require(g_displayed.empty() && g_queued.empty(), "impact before reply waits for authority");
    g_time = 240;
    Receive(1, 75, true, 1);
    Require(g_queued.size() == 1 && g_queued[0].damage == 75 && g_queued[0].critical,
        "late reply is released only after impact");
    Receive(1, 75, true, 1);
    Require(g_queued.size() == 1, "duplicate reply does not draw twice");

    ResetCase();
    Track(Packet(0, {80, 40}));
    Track(Packet(0, {80, 40}));
    Receive(1, 60, false, 1);
    Receive(1, 90, true, 1);
    Impact(1, 40, 1);
    Impact(1, 40, 1);
    Require(g_displayed.size() == 2 && g_displayed[0].damage == 60 && g_displayed[1].damage == 90
        && g_displayed[1].critical == 1, "repeated identical native values consume replies in attack order");

    ResetCase();
    Track(Packet(4001344, {100, 100, 50 | INT_MIN, 0}));
    Receive(1, 75, true, 2);
    Receive(1, 0, false, 3);
    ShadowPartnerDamageSync::BeginIncomingPacket();
    ShadowPartnerDamageSync::BeginIncomingPacket();
    Impact(1, 50, 2);
    ShadowPartnerDamageSync::EndIncomingPacket();
    Impact(1, 50, 2);
    ShadowPartnerDamageSync::EndIncomingPacket();
    Impact(1, 50, 2);
    Impact(1, 0, 3);
    Require(g_displayed.size() == 4 && g_displayed[0].damage == 50 && g_displayed[1].damage == 50
        && g_displayed[2].damage == 75 && g_displayed[2].critical == 1 && g_displayed[3].damage == 0,
        "remote nested context cannot consume local critical or MISS");

    for (int skill : {0, 4101005, 4111004, 4111005, 4121008}) {
        ResetCase();
        Track(Packet(skill, {1, 1}));
        Receive(1, 1, false, 1);
        Impact(1, 1, 0);
        Impact(1, 1, 1);
        Require(g_displayed.size() == 2 && g_queued.empty(), "equal body and immune copied values remain separate");
        Track(Packet(skill, {1}));
        Impact(1, 1, 0);
        Require(g_displayed.size() == 3, "non-copied attack keeps native display");
    }

    ResetCase();
    Track(Packet(0, {80, 40}));
    Receive(1, 999, true, 1, false);
    Require(g_queued.size() == 1 && g_queued[0].damage == 999, "unmarked damage is not mistaken for a copied reply");
    Receive(1, 60, false, 1);
    g_lookupAvailable = false;
    Require(g_displayed.empty(), "removal does not flush cached copied damage before impact");
    Impact(1, 40, 1);
    Require(g_displayed.size() == 1 && g_displayed[0].damage == 60, "native lethal callback uses cache without another mob lookup");

    ResetCase();
    Track(Packet(0, {80, 40}));
    Receive(1, 60, false, 1);
    ShadowPartnerDamageSync::Reset();
    Receive(1, 60, false, 1);
    Impact(1, 40, 1);
    Require(g_queued.empty() && g_displayed[0].damage == 40, "map reset discards stale replies and preserves unmatched native hits");

    ResetCase();
    Track(Packet(0, {80, 40}));
    g_pending[0].recordedAt = GetTickCount() - kPendingMs - 1;
    Receive(1, 60, false, 1);
    Impact(1, 40, 1);
    Require(g_queued.empty() && g_displayed[0].damage == 40, "expired reply cannot cause a late extra number");

    for (int skill : {4121003, 4121004, 14001004, 3121004}) {
        ResetCase();
        Track(Packet(skill, {100, 50}));
        Impact(1, 50, 1);
        Require(g_displayed.size() == 1 && g_displayed[0].damage == 50, "other skills remain untouched");
    }
    ResetCase();
    auto truncated = Packet(4121007, {80, 80, 80, 40, 40, 40});
    truncated.pop_back();
    Track(truncated);
    Impact(1, 40, 3);
    Require(g_displayed.size() == 1 && g_displayed[0].damage == 40, "truncated packets do not suppress native damage");
    std::puts("PASS: production hooks preserve projectile impact timing, line cadence, authority, critical/MISS, FIFO, isolation and reset");
}
