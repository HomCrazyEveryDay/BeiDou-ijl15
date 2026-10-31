#include "stdafx.h"
#include "VisitorsTimerTooltip.h"
#include "ClientLog.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <climits>

namespace {
#ifdef VISITORS_TIMER_TOOLTIP_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
constexpr int timerItem = 4032735;
constexpr ULONGLONG ticksPerMinute = 600000000ull;
constexpr ULONGLONG duration = 60 * ticksPerMinute;
// All ordinary wire dates are FILETIME, including the server's timezone offset.
// Reject permanent/sentinel values before doing unsigned time arithmetic.
bool Date(ULONGLONG value) { return value >= 125911584000000000ull && value < 157469184000000000ull; }
auto tickCount = &GetTickCount64;
auto protectMemory = &VirtualProtect;
bool installed = false;
DWORD uiThread = 0;
LONG failureLogged = 0;
struct Clock { ULONGLONG stamp = 0, tick = 0; unsigned generation = 0; };
Clock clockState;
SRWLOCK clockLock = SRWLOCK_INIT;
struct Time { ULONGLONG stamp; unsigned generation; };
Time Now(ULONGLONG tick) {
    AcquireSRWLockShared(&clockLock);
    const auto clock = clockState;
    ReleaseSRWLockShared(&clockLock);
    if (!clock.stamp || tick < clock.tick || tick - clock.tick > 30ull * 86400000) return {0, clock.generation};
    return {clock.stamp + (tick - clock.tick) * 10000, clock.generation};
}
int Minutes(ULONGLONG expiration, ULONGLONG now) {
    if (!Date(expiration) || !Date(now)) return -1;
    if (expiration <= now) return 60;
    const auto remaining = expiration - now;
    return remaining >= duration ? 0 : static_cast<int>((duration - remaining) / ticksPerMinute);
}
struct Arguments { DWORD values[8]; };
static_assert(sizeof(Arguments) == 0x20, "CUIToolTip::SetToolTip_Item RET 20, borrowed arguments");
using Builder = void(__thiscall*)(void*, Arguments);
using NameGetter = void*(__thiscall*)(void*, void*, int);
using Update = void(__thiscall*)(void*);
Builder originalBuilder = nullptr;
const auto originalName = reinterpret_cast<NameGetter>(Native(0x005CF63E));
const auto originalUpdate = reinterpret_cast<Update>(Native(0x0081E255));
struct Title { int minutes = -1; };
thread_local Title* currentTitle = nullptr;
struct Hover {
    void* owner = nullptr;
    void* window = nullptr;
    void* layer = nullptr;
    void* character = nullptr;
    ULONGLONG expiration = 0, nextCheck = 0;
    unsigned generation = 0;
    int minutes = -1, x = 0, y = 0;
};
Hover hover;
void* Inventory() { return *reinterpret_cast<void**>(Native(0x00BED654)); }
void* Character() {
    auto context = *reinterpret_cast<BYTE**>(Native(0x00BE7918));
    return context ? *reinterpret_cast<void**>(context + 0x20B8) : nullptr;
}
void* Layer(void* owner) { return *reinterpret_cast<void**>(static_cast<BYTE*>(owner) + 0x10); }
bool ItemTime(void* item, ULONGLONG& expiration) {
    if (!item) return false;
    using Read = int(__thiscall*)(void*);
    if (reinterpret_cast<Read>(Native(0x0042873D))(static_cast<BYTE*>(item) + 0x0C) != timerItem) return false;
    // GW_ItemSlotBase::Decode at 004E36DC reads eight wire bytes into +20.
    std::memcpy(&expiration, static_cast<BYTE*>(item) + 0x20, sizeof(expiration));
    return Date(expiration);
}
void* __fastcall NameHook(void* info, void*, char** result, int itemId) {
    void* returned = originalName(info, result, itemId);
    if (!installed || itemId != timerItem || !currentTitle || currentTitle->minutes < 0 || !*result) return returned;
    // Keep the actual WZ name and replace its fixed suffix. Assigning a new
    // ZXString preserves the shared String resource and its reference count.
    const char* suffix = std::strstr(*result, "\xa3\xa8"); // GBK full-width (
    if (!suffix) suffix = std::strchr(*result, '(');
    if (!suffix || suffix - *result > 128) return returned;
    wchar_t wide[48]{};
    swprintf_s(wide, L"\uff08\u5df2\u8fc7%d\u5206\u949f\uff09", currentTitle->minutes);
    char text[192]{};
    const int prefix = static_cast<int>(suffix - *result);
    std::memcpy(text, *result, prefix);
    if (!WideCharToMultiByte(936, 0, wide, -1, text + prefix, sizeof(text) - prefix, nullptr, nullptr)) return returned;
    using Assign = void(__thiscall*)(void*, const char*, int);
    reinterpret_cast<Assign>(Native(0x00414617))(result, text, -1);
    return returned;
}
void __fastcall BuildHook(void* owner, void*, Arguments args) {
    if (!installed) { originalBuilder(owner, args); return; }
    if (!uiThread) uiThread = GetCurrentThreadId();
    if (uiThread != GetCurrentThreadId()) { originalBuilder(owner, args); return; }
    const ULONGLONG tick = tickCount();
    const Time now = Now(tick);
    ULONGLONG expiration = 0;
    Title title;
    if (ItemTime(reinterpret_cast<void*>(args.values[2]), expiration)) title.minutes = Minutes(expiration, now.stamp);
    if (hover.owner == owner && (title.minutes != hover.minutes || expiration != hover.expiration
        || now.generation != hover.generation)) {
        // Native builders cache solely by mouse x/y. Invalidate only this
        // tooltip when its displayed minute/identity changes, never each frame.
        *reinterpret_cast<int*>(static_cast<BYTE*>(owner) + 0x14) = INT_MIN;
    }
    if (hover.owner == owner) hover = {};
    Title* previous = currentTitle;
    currentTitle = &title;
    __try { originalBuilder(owner, args); }
    __finally { currentTitle = previous; }
    void* window = Inventory();
    if (title.minutes >= 0 && window && owner == static_cast<BYTE*>(window) + 0x74 && Layer(owner)) {
        hover = {owner, window, Layer(owner), Character(), expiration, tick + 250,
            now.generation, title.minutes, static_cast<int>(args.values[0]), static_cast<int>(args.values[1])};
    }
}
void Refresh(void* window) {
    if (!installed || !hover.owner || GetCurrentThreadId() != uiThread) return;
    if (Inventory() != hover.window || window != hover.window || Character() != hover.character) { hover = {}; return; }
    if (Layer(hover.owner) != hover.layer) { hover = {}; return; }
    const auto tick = tickCount();
    if (tick < hover.nextCheck) return;
    hover.nextCheck = tick + 250;
    const auto now = Now(tick);
    if (!now.stamp || now.generation != hover.generation) { hover = {}; return; }
    if (Minutes(hover.expiration, now.stamp) == hover.minutes) return;
    // Re-enter the original inventory hover path, which reacquires the current
    // slot/item. Never retain a GW_ItemSlotBase, ZRef, or stack options pointer.
    auto mouseOwner = static_cast<BYTE*>(window) + 4;
    const auto methods = *reinterpret_cast<void***>(mouseOwner);
    using Coordinate = int(__thiscall*)(void*);
    const int x = hover.x - reinterpret_cast<Coordinate>(methods[0x2C / 4])(mouseOwner);
    const int y = hover.y - reinterpret_cast<Coordinate>(methods[0x30 / 4])(mouseOwner) - 20;
    void* owner = hover.owner;
    const int oldMinute = hover.minutes;
    using MouseMove = int(__thiscall*)(void*, int, int);
    reinterpret_cast<MouseMove>(Native(0x0081D8AD))(mouseOwner, x, y);
    if (hover.owner == owner && hover.minutes == oldMinute) {
        // A now-empty/out-of-range slot can return without building or clearing
        // anything. Remove its stale tooltip instead of retrying every tick.
        reinterpret_cast<void(__thiscall*)(void*)>(Native(0x008E6E23))(owner);
    }
}
void __fastcall UpdateHook(void* window, void*) {
    originalUpdate(window); // Includes the existing main-pet inventory mark.
    Refresh(window);
}
struct Signature { DWORD address; const char* bytes; size_t size; };
const Signature signatures[] = {
    {Native(0x008F5B20), "\xb8\xd4\x5c\xad\x00", 5},
    {Native(0x008EE57A), "\xe8\xbf\x10\xce\xff", 5},
    {Native(0x00B39108), "\x55\xe2\x81\x00", 4},
    {Native(0x008F5D15), "\x56\x8b\xcb\xe8\x0c\x25\xb3\xff", 8},
    {Native(0x004E36DC), "\x6a\x08\x83\xc6\x20\x56\x8b\xcf\xe8\x6e\xeb\xf4\xff", 13},
    {Native(0x0081D96E), "\x8d\x4e\x70\x50\xe8\xa9\x81\x0d\x00", 9},
    {Native(0x0042873D), "\x55\x8b\xec\x83\xec\x10\x8b\x41\x08", 9},
    {Native(0x005CF63E), "\xb8\x6b\x3b\xa9\x00", 5},
    {Native(0x00414617), "\x83\x7c\x24\x04\x00\x57\x8b\xf9", 8}
};
void Relative(BYTE* site, BYTE opcode, const void* target) {
    site[0] = opcode;
    const DWORD displacement = reinterpret_cast<DWORD>(target) - reinterpret_cast<DWORD>(site) - 5;
    std::memcpy(site + 1, &displacement, 4);
}
bool Failure(const char* reason) {
    if (InterlockedCompareExchange(&failureLogged, 1, 0) == 0)
        ClientLog::Append(ClientLog::Component::Lifecycle, "event=visitors_timer_tooltip_install success=0 reason=%s", reason);
    return false;
}
}

void VisitorsTimerTooltip::ObservePacket(const unsigned char* data, unsigned long size) {
    if (!installed || !data || size < 6) return;
    const unsigned opcode = data[4] | (data[5] << 8);
    if (opcode != 0 && opcode != 0x7D) return;
    ULONGLONG stamp = 0;
    // Both full character entry and ordinary map warps end in the same eight
    // server-time bytes (PacketCreator.getCharInfo/getWarpToMap).
    if (opcode == 0x7D && size >= 31) std::memcpy(&stamp, data + size - 8, 8);
    AcquireSRWLockExclusive(&clockLock);
    clockState = {Date(stamp) ? stamp : 0, tickCount(), clockState.generation + 1};
    ReleaseSRWLockExclusive(&clockLock);
}
void VisitorsTimerTooltip::Clear(void* owner) {
    if (hover.owner == owner) hover = {};
}
bool VisitorsTimerTooltip::Install(HMODULE module) {
    if (installed) return true;
    if (reinterpret_cast<DWORD>(module) != Native(0x00400000)) return Failure("base");
    const auto* base = reinterpret_cast<const BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return Failure("dos");
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != 0x4B7C15C9
        || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt->OptionalHeader.SizeOfImage != 0xA94000) return Failure("image");
    for (const auto& signature : signatures)
        if (std::memcmp(reinterpret_cast<void*>(signature.address), signature.bytes, signature.size)) return Failure("signature");
    auto trampoline = static_cast<BYTE*>(VirtualAlloc(nullptr, 16, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!trampoline) return Failure("allocate");
    std::memcpy(trampoline, signatures[0].bytes, 5);
    Relative(trampoline + 5, 0xE9, reinterpret_cast<void*>(Native(0x008F5B25)));
    DWORD ignored;
    if (!protectMemory(trampoline, 16, PAGE_EXECUTE_READ, &ignored)
        || !FlushInstructionCache(GetCurrentProcess(), trampoline, 16)) {
        VirtualFree(trampoline, 0, MEM_RELEASE); return Failure("trampoline");
    }
    originalBuilder = reinterpret_cast<Builder>(trampoline);
    std::array<DWORD, 3> previous{};
    size_t opened = 0;
    for (; opened < previous.size(); ++opened) {
        const auto& site = signatures[opened];
        if (!protectMemory(reinterpret_cast<void*>(site.address), site.size, PAGE_EXECUTE_READWRITE, &previous[opened])) break;
    }
    bool success = opened == previous.size();
    if (success) {
        Relative(reinterpret_cast<BYTE*>(signatures[0].address), 0xE9, BuildHook);
        Relative(reinterpret_cast<BYTE*>(signatures[1].address), 0xE8, NameHook);
        *reinterpret_cast<void**>(signatures[2].address) = UpdateHook;
        success = FlushInstructionCache(GetCurrentProcess(), base, nt->OptionalHeader.SizeOfImage) != FALSE;
    }
    for (size_t i = 0; i < opened; ++i)
        if (!protectMemory(reinterpret_cast<void*>(signatures[i].address), signatures[i].size, previous[i], &ignored)) success = false;
    if (!success) {
        bool reverted = true;
        for (size_t i = 0; i < opened; ++i) {
            const auto& site = signatures[i];
            if (!protectMemory(reinterpret_cast<void*>(site.address), site.size, PAGE_EXECUTE_READWRITE, &ignored)) { reverted = false; continue; }
            std::memcpy(reinterpret_cast<void*>(site.address), site.bytes, site.size);
            if (!protectMemory(reinterpret_cast<void*>(site.address), site.size, previous[i], &ignored)) reverted = false;
        }
        if (!FlushInstructionCache(GetCurrentProcess(), base, nt->OptionalHeader.SizeOfImage)) reverted = false;
        if (reverted) VirtualFree(trampoline, 0, MEM_RELEASE);
        return Failure("protection/cache");
    }
    installed = true;
    ClientLog::Append(ClientLog::Component::Lifecycle, "event=visitors_timer_tooltip_install success=1 localMinutes=1");
    return true;
}
