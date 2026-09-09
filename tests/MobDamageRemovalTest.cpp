#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "MobDamageTypes.h"

static void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static DWORD g_now = 5000;
static DWORD TestTickCount() { return g_now; }
#define GetTickCount TestTickCount

namespace CrashReporter {
static void RecordEvent(const char*, const char*, ...) {}
static void RecordRecentEvent(const char*, const char*, ...) {}
static int CaptureHandledException(const char*, const char*, EXCEPTION_POINTERS*) {
    Require(false, "unexpected native rendering exception");
    return EXCEPTION_EXECUTE_HANDLER;
}
}

struct TestMob { int objectId; bool alive; };
static TestMob g_mobs[] = {{101, true}, {202, true}};
struct Displayed { int objectId, damage, line, critical; };
static std::vector<Displayed> g_displayed;
static int g_nativeCalls, g_incomingDepth;
static bool g_resourcesReady = true;

static bool TryReadDword(DWORD address, DWORD& value) {
    Require(address == kMobPoolPtr, "lookup uses native mob pool");
    value = 1;
    return true;
}
static bool ReadDamageResources(bool, DWORD& displayer, DWORD& first, DWORD& second) {
    displayer = first = second = 1;
    return g_resourcesReady;
}
static void* g_FindMob(void*, int objectId) {
    for (auto& mob : g_mobs) {
        if (mob.objectId == objectId && mob.alive) return &mob;
    }
    return nullptr;
}
static void g_ShowMobDamage(void* ptr, void*, int damage, int line, int critical, int) {
    auto& mob = *static_cast<TestMob*>(ptr);
    Require(mob.alive && g_renderingServerMobDamage, "server number renders while mob still exists and bypasses suppression");
    g_displayed.push_back({mob.objectId, damage, line, critical});
}
static void s_ProcessPacket(void*, void*, CInPacket* packet) {
    ++g_nativeCalls;
    if (!packet || !packet->Data || packet->DataLen < 11) return;
    const auto* bytes = static_cast<const unsigned char*>(packet->Data);
    unsigned short opcode;
    int objectId;
    memcpy(&opcode, bytes + 4, 2);
    memcpy(&objectId, bytes + 6, 4);
    if (opcode == kOpcodeKillMonster) {
        for (auto& mob : g_mobs) {
            if (mob.objectId == objectId) mob.alive = false;
        }
    }
}

namespace MineralBagWnd { static bool HandlePacket(const unsigned char*, unsigned long) { return false; } }
namespace ClientDiagnostics { static bool HandleIncoming(const unsigned char*, unsigned long) { return false; } }
namespace AbsoluteDefenseSync {
static bool HandlePacket(const unsigned char*, unsigned long) { return false; }
static void BeginIncomingAttackPacket(const unsigned char*, unsigned long) {}
static void EndIncomingAttackPacket() {}
}
namespace IntegratedFinalAttack { static bool HandlePacket(CInPacket*) { return false; } }
namespace StackedBuffIcons { static bool HandlePacket(CInPacket*) { return false; } }
namespace HurricaneDamageSync {
static void BeginIncomingPacket() { ++g_incomingDepth; }
static void EndIncomingPacket() { --g_incomingDepth; }
}
namespace ShadowPartnerDamageSync {
static void TrackIncomingAttackPacket(const unsigned char*, unsigned long) {}
static void BeginIncomingPacket() {}
static void EndIncomingPacket() {}
}
static void TraceIncomingPacket(CInPacket*) {}
static void ObserveBossVenomStatusPacket(CInPacket*) {}
static bool HandleShowMobDamagePacket(CInPacket*) { return false; }
static void HandleHpMpAlertPacket(CInPacket*) {}

// Compiles the production queue, rendering, packet dispatch and frame update.
#include "MobDamageUnderTest.h"

static void ResetCase() {
    g_mobDamageQueue.clear();
    g_displayed.clear();
    g_nativeCalls = g_incomingDepth = 0;
    g_now = 5000;
    g_resourcesReady = true;
    g_mobDamageFieldActive = true;
    g_mobDamageFieldGeneration = 7;
    g_mobDamageFrame = 10;
    for (auto& mob : g_mobs) mob.alive = true;
}
static void Queue(int objectId, int damage, bool critical = false, int line = 0) {
    QueuedMobDamage queued{};
    queued.objectId = objectId;
    queued.damage = damage;
    queued.critical = critical;
    queued.lineIndex = line;
    queued.sequence = 1;
    Require(QueueMobDamage(queued), "enqueue server damage");
}
static void Packet(int objectId, unsigned short opcode = kOpcodeKillMonster, unsigned short size = 11) {
    unsigned char bytes[16]{};
    memcpy(bytes + 4, &opcode, 2);
    memcpy(bytes + 6, &objectId, 4);
    bytes[10] = 1;
    CInPacket packet{};
    packet.Data = bytes;
    packet.DataLen = size;
    ProcessPacket_Hook(nullptr, nullptr, &packet);
    Require(g_incomingDepth == 0, "incoming context is balanced");
}

int main() {
    InitializeCriticalSection(&g_mobDamageQueueLock);
    g_mobDamageQueueLockInitialized = true;

    ResetCase();
    Queue(101, 91816, true);
    Packet(101);
    Require(g_displayed.size() == 1 && g_displayed[0].damage == 91816 && g_displayed[0].critical == 1,
        "same-frame lethal critical hit keeps its damage number");
    Require(!g_mobs[0].alive && g_nativeCalls == 1, "native death packet still removes the mob once");
    UpdateQueuedMobDamageDisplay();
    Require(g_displayed.size() == 1 && g_mobDamageQueue.empty(), "lethal number is not drawn twice next frame");

    ResetCase();
    for (int line = 0; line < 7; ++line) {
        Queue(101, 100 + line, line % 2 != 0, line);
        Queue(202, 200 + line, false, line);
    }
    Packet(101);
    Require(g_displayed.size() == 7 && g_mobDamageQueue.size() == 7,
        "all dying-target lines flush beyond frame budget and other targets stay queued");
    for (int line = 0; line < 7; ++line) {
        Require(g_displayed[line].line == line && g_displayed[line].damage == 100 + line,
            "death flush preserves number order and line indexes");
    }
    UpdateQueuedMobDamageDisplay();
    Require(g_displayed.size() == 11 && g_mobDamageQueue.size() == 3, "surviving target keeps four-number frame budget");
    UpdateQueuedMobDamageDisplay();
    Require(g_displayed.size() == 14 && g_mobDamageQueue.empty(), "surviving target finishes normally");

    ResetCase();
    Queue(101, 50);
    g_mobDamageQueue.back().queuedAt = g_now - kQueuedDamageTtlMs - 1;
    Queue(101, 60);
    --g_mobDamageQueue.back().fieldGeneration;
    Queue(202, 70);
    Packet(101);
    Require(g_displayed.empty() && g_mobDamageQueue.size() == 1,
        "expired and previous-map numbers are discarded without consuming another target");

    ResetCase();
    Queue(101, 80);
    Packet(101, 0xEF);
    Packet(101, kOpcodeKillMonster, 10);
    ProcessPacket_Hook(nullptr, nullptr, nullptr);
    Require(g_displayed.empty() && g_mobDamageQueue.size() == 1 && g_mobs[0].alive,
        "movement, truncated and null packets do not flush damage");
    UpdateQueuedMobDamageDisplay();
    Require(g_displayed.size() == 1, "nonlethal zero-focus hit still renders on the next frame");

    ResetCase();
    Queue(101, 100);
    g_resourcesReady = false;
    UpdateQueuedMobDamageDisplay();
    Require(g_displayed.empty() && g_mobDamageQueue.size() == 1, "resource retry retains the server number");
    g_resourcesReady = true;
    Packet(101);
    Require(g_displayed.size() == 1 && g_mobDamageQueue.empty(), "death flush includes a ready retry");

    ResetCase();
    Queue(101, 0);
    Queue(101, 1);
    Packet(101);
    Require(g_displayed.size() == 2 && g_displayed[0].damage == 0 && g_displayed[1].damage == 1,
        "MISS and immunity numbers survive target removal");
    DeleteCriticalSection(&g_mobDamageQueueLock);
    std::puts("PASS: lethal damage timing, criticals, queue order, frame budget, expiry, map isolation, retries and MISS");
}
