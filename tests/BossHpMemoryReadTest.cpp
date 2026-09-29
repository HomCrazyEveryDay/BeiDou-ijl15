#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define private public
#include "../ezorsia/BossHP.h"
#undef private

static BYTE* miniMap;
const DWORD dw_TSingleton_CUIMiniMap___ms_pInstance = reinterpret_cast<DWORD>(&miniMap);
char BossHP::aBossHpUIToolTip[1304];
double BossHP::dBossHpPercentage;
static int tooltipCalls, tooltipX, tooltipY;
static char tooltipText[20];
static volatile LONG stopWriter, writeCount, writeFailures;
static HANDLE firstWrite;
static volatile DWORD* soundState;

static void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static BOOL WINAPI UnexpectedProtect(LPVOID, SIZE_T, DWORD, PDWORD) {
    Require(false, "a display read must not change shared page permissions");
    return FALSE;
}
#define VirtualProtect UnexpectedProtect
#include "BossHpReadUnderTest.h"
#undef VirtualProtect

void BossHP::SetToolTip_String(int instance, int x, int y, const char* text) {
    Require(instance == reinterpret_cast<int>(aBossHpUIToolTip), "tooltip instance preserved");
    ++tooltipCalls;
    tooltipX = x;
    tooltipY = y;
    strcpy_s(tooltipText, text);
}

static bool WriteSoundState() {
    __try {
        *soundState = 0;
        return true;
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

static DWORD WINAPI Writer(void*) {
    while (InterlockedCompareExchange(&stopWriter, 0, 0) == 0) {
        if (!WriteSoundState()) InterlockedIncrement(&writeFailures);
        InterlockedIncrement(&writeCount);
        SetEvent(firstWrite);
    }
    return 0;
}

int main() {
    static_assert(sizeof(void*) == 4, "Run with the game's x86 pointer size");
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    Require(systemInfo.dwPageSize >= 0x9c4, "test objects fit on the same page");
    auto page = static_cast<BYTE*>(VirtualAlloc(nullptr, systemInfo.dwPageSize,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(page != nullptr, "allocate isolated shared page");
    // Same within-page positions as the player's minimap and sound state.
    miniMap = page + 0x99c;
    auto width = reinterpret_cast<int*>(miniMap + 0x24);
    soundState = reinterpret_cast<volatile DWORD*>(page + 0x5d0);
    *width = 300;
    *soundState = 1;
    BossHP::dBossHpPercentage = 71.69219;
    BossHP::DrawBossHpNumberIfNeed();
    Require(tooltipCalls == 1 && tooltipX == 300 && tooltipY == 37
        && std::strcmp(tooltipText, "71.69%") == 0, "percentage and placement preserved");

    miniMap = nullptr;
    BossHP::DrawBossHpNumberIfNeed();
    Require(tooltipCalls == 1 && BossHP::dBossHpPercentage == 71.69219,
        "missing minimap skips only the current frame");
    miniMap = page + 0x99c;
    *width = 420;
    BossHP::DrawBossHpNumberIfNeed();
    Require(tooltipCalls == 2 && tooltipX == 420, "display recovers with current minimap width");

    BossHP::dBossHpPercentage = 0;
    miniMap = reinterpret_cast<BYTE*>(1);
    BossHP::DrawBossHpNumberIfNeed();
    Require(tooltipCalls == 2, "inactive boss display does not access minimap");
    miniMap = page + 0x99c;
    BossHP::dBossHpPercentage = 71.69219;

    firstWrite = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Require(firstWrite != nullptr, "create writer signal");
    HANDLE writer = CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr);
    Require(writer != nullptr, "create concurrent writer");
    Require(WaitForSingleObject(firstWrite, 5000) == WAIT_OBJECT_0, "writer is running");
    for (int frame = 0; frame < 20000; ++frame) {
        BossHP::DrawBossHpNumberIfNeed();
    }
    InterlockedExchange(&stopWriter, 1);
    Require(WaitForSingleObject(writer, 5000) == WAIT_OBJECT_0, "writer completed");
    Require(writeCount > 0 && writeFailures == 0 && *soundState == 0,
        "concurrent writes to the shared page succeed");
    Require(tooltipCalls == 20002 && tooltipX == 420 && *width == 420,
        "repeated display reads preserve width");
    MEMORY_BASIC_INFORMATION memory{};
    Require(VirtualQuery(page, &memory, sizeof(memory)) != 0
        && memory.Protect == PAGE_READWRITE, "shared page remains writable");
    CloseHandle(writer);
    CloseHandle(firstWrite);
    VirtualFree(page, 0, MEM_RELEASE);
    std::puts("PASS: display formatting/placement, missing minimap/recovery, inactive display, 20000 frames with shared-page writes and no protection changes");
}
