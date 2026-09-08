#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
#include "BuffIconTypes.h"

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

DWORD g_now = 5000;
DWORD TestTickCount() { return g_now; }
#define GetTickCount TestTickCount

DWORD g_context[0x3000 / sizeof(DWORD)]{};
DWORD TestView() { return reinterpret_cast<DWORD>(g_context) + kTemporaryStatViewContextOffset; }
DWORD& Word(DWORD address) { return *reinterpret_cast<DWORD*>(address); }
struct TestNode {
    DWORD unused0, nextRaw, unused2, unused3, nodeData, entryPtr;
    DWORD entry[16];
};
std::vector<TestNode*> g_nodes;
bool g_canAdd = true;
int g_draws = 0;

void DebugLog(const char*, ...) {}
void __fastcall TestNativeDraw(DWORD view, void*) {
    Require(view == TestView(), "draw uses the context-owned buff bar");
    ++g_draws;
}
void __fastcall NativeDrawHook(DWORD, void*);
void DrawNativeTemporaryStatView(DWORD view) { NativeDrawHook(view, nullptr); }

bool TryCallNativeAddIcon(DWORD view, int type, int id, int duration) {
    Require(view == TestView(), "add uses the context-owned buff bar");
    if (!g_canAdd) return false;
    auto* node = new TestNode{};
    const DWORD oldHead = Word(view + kTemporaryStatViewListHeadOffset);
    node->nextRaw = oldHead ? oldHead - 0x10 : 0;
    node->entryPtr = reinterpret_cast<DWORD>(node->entry);
    node->entry[0x1C / 4] = type;
    node->entry[0x20 / 4] = id;
    node->entry[0x38 / 4] = duration;
    Word(view + kTemporaryStatViewListHeadOffset) = reinterpret_cast<DWORD>(&node->nodeData);
    ++Word(view + kTemporaryStatViewListCountOffset);
    g_nodes.push_back(node);
    DrawNativeTemporaryStatView(view);
    return true;
}
bool TryRemoveNativeNode(DWORD view, DWORD node) {
    DWORD* link = reinterpret_cast<DWORD*>(view + kTemporaryStatViewListHeadOffset);
    DWORD current = *link;
    while (current) {
        const DWORD rawNext = Word(current - 0x0C);
        if (current == node) {
            *link = link == reinterpret_cast<DWORD*>(view + kTemporaryStatViewListHeadOffset)
                ? (rawNext ? rawNext + 0x10 : 0) : rawNext;
            --Word(view + kTemporaryStatViewListCountOffset);
            return true;
        }
        link = reinterpret_cast<DWORD*>(current - 0x0C);
        current = rawNext ? rawNext + 0x10 : 0;
    }
    return false;
}

// Exercise production packet parsing, pointer validation, native list reconciliation,
// draw interception and field lifecycle; only the native UI calls are substituted.
#include "BuffIconsUnderTest.h"

int CountIcon(int id) {
    const auto counts = CountNativeIconsByKey(TestView());
    const auto found = counts.find(NativeIconKey(kNativeIconTypeSkill, id));
    return found == counts.end() ? 0 : found->second;
}
void SendFocus(BYTE stacks) {
    BYTE data[] = {0, 0, 0, 0, 0x09, 0x10, stacks};
    CInPacket packet{};
    packet.Data = data;
    packet.DataLen = sizeof(data);
    Require(StackedBuffIcons::HandlePacket(&packet), "focus packet is consumed");
}
void ResetCase() {
    for (auto* node : g_nodes) delete node;
    g_nodes.clear();
    memset(g_context, 0, sizeof(g_context));
    Word(kWvsContextPtr) = reinterpret_cast<DWORD>(g_context);
    Word(TestView()) = kTemporaryStatViewVtable;
    Word(TestView() + 4) = kTemporaryStatViewListVtable;
    g_virtualNativeNodes.clear();
    g_icons.clear();
    g_canAdd = true;
    g_draws = 0;
    g_now = 5000;
    g_syncingNativeIcons = false;
    g_lastNativeSyncAttempt = 0;
    StackedBuffIcons::OnFieldInit();
}

void TestMapTransition() {
    ResetCase();
    StackedBuffIcons::OnFieldUpdate();
    // An earlier native buff draw establishes the view, then maps change without
    // native buff packets. This matches the missing-icon client log.
    TryCallNativeAddIcon(TestView(), kNativeIconTypeSkill, 3121002, 60000);
    StackedBuffIcons::OnFieldInit();
    StackedBuffIcons::OnFieldDispose();
    StackedBuffIcons::OnFieldUpdate();
    SendFocus(1);
    Require(CountIcon(kBowExpert) == 1, "focus appears after a map change without another native buff");
    for (BYTE stacks = 2; stacks <= 5; ++stacks) SendFocus(stacks);
    Require(CountIcon(kBowExpert) == 1 && g_hurricaneFocusStacks == 5,
        "stack updates reuse one icon and expose the latest overlay value");
    Require(g_lastNativeDrawView == TestView(), "synthetic draws retain the number-overlay view");
    StackedBuffIcons::OnFieldInit();
    StackedBuffIcons::OnFieldDispose();
    Require(!g_countdownFieldActive && !g_lastNativeDrawView, "map transition suspends the overlay");
    StackedBuffIcons::OnFieldUpdate();
    Require(CountIcon(kBowExpert) == 0 && CountIcon(3121002) == 1,
        "map reset removes owned focus while preserving an ordinary native buff");
    SendFocus(1);
    SendFocus(0);
    Require(CountIcon(kBowExpert) == 0 && g_hurricaneFocusStacks == 0,
        "zero stacks remove the icon after rebuilding it in the next map");
}

void TestFirstLoginAndDeferredSync() {
    ResetCase();
    SendFocus(3);
    Require(CountIcon(kBowExpert) == 0 && g_nativeSyncPending, "packets before field activation are deferred");
    StackedBuffIcons::OnFieldUpdate();
    Require(CountIcon(kBowExpert) == 1 && g_hurricaneFocusStacks == 3,
        "first login displays focus even if no ordinary buff was ever cast");
    SendFocus(6);
    Require(g_hurricaneFocusStacks == 3, "invalid stack count is ignored");

    ResetCase();
    Word(kWvsContextPtr) = 0;
    StackedBuffIcons::OnFieldUpdate();
    SendFocus(2);
    Require(CountIcon(kBowExpert) == 0 && g_nativeSyncPending, "absent context defers focus");
    Word(kWvsContextPtr) = reinterpret_cast<DWORD>(g_context);
    Word(TestView() + 4) = 0;
    StackedBuffIcons::OnFieldUpdate();
    Require(CountIcon(kBowExpert) == 0, "partially initialized native view is rejected");
    Word(TestView() + 4) = kTemporaryStatViewListVtable;
    StackedBuffIcons::OnFieldUpdate();
    Require(CountIcon(kBowExpert) == 1 && !g_nativeSyncPending,
        "deferred focus retries when the native view becomes usable");

    ResetCase();
    StackedBuffIcons::OnFieldUpdate();
    g_canAdd = false;
    SendFocus(1);
    Require(g_nativeSyncPending && CountIcon(kBowExpert) == 0, "failed native add remains pending");
    g_canAdd = true;
    StackedBuffIcons::OnFieldUpdate();
    Require(CountIcon(kBowExpert) == 0, "native add retries are rate limited");
    g_now += kNativeSyncRetryInterval;
    StackedBuffIcons::OnFieldUpdate();
    Require(CountIcon(kBowExpert) == 1, "failed native add recovers without another stack packet");
    const int draws = g_draws;
    g_now += 1000;
    StackedBuffIcons::OnFieldUpdate();
    Require(g_draws == draws, "steady state does not rebuild the buff bar every frame");
}
} // namespace

int main() {
    const DWORD page = kWvsContextPtr & ~0xFFFFu;
    Require(VirtualAlloc(reinterpret_cast<void*>(page), 0x10000, MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE) != nullptr, "reserve native context pointer address");
    InitializeCriticalSection(&g_iconLock);
    g_iconLockInitialized = true;
    g_nativeDraw = TestNativeDraw;
    TestMapTransition();
    TestFirstLoginAndDeferredSync();
    ResetCase();
    DeleteCriticalSection(&g_iconLock);
    VirtualFree(reinterpret_cast<void*>(page), 0, MEM_RELEASE);
    std::puts("PASS: focus icon map transitions, first login, cleanup, pointer validation and deferred recovery");
}
