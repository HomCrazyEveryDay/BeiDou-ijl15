#pragma once
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <cstdio>
#include <intrin.h>
#include "Memory.h"
#include "ClientLog.h"

// v83: recover a missing timer via the original scheduler; no action/HP/packet edits.
namespace DeathDialogDiagnostics {
#ifdef DEATH_DIALOG_DIAGNOSTICS_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
using Action = void(__thiscall*)(void*, int, int);
using Update = void(__thiscall*)(void*);
using Create = void*(__cdecl*)();
using Cancel = void(__thiscall*)(void*);
using Arm = void(__thiscall*)(void*);
static Action action = reinterpret_cast<Action>(Native(0x0092ECD1));
static Update update = reinterpret_cast<Update>(Native(0x00A03350));
static Create create = reinterpret_cast<Create>(Native(0x00A14AFC));
static Cancel cancel = reinterpret_cast<Cancel>(Native(0x00A0670C));
static Arm arm = reinterpret_cast<Arm>(Native(0x00A066F3));
static bool recovery = true, logging = true, installed = false;

struct Snapshot {
    void* user = nullptr;
    void* context = nullptr;
    void* character = nullptr;
    void* dialog = nullptr;
    DWORD tick = 0, pending = 0, effect = 0;
    int stance = -1, hp = -1;
    bool valid = false;
};
// CharacterData::HP (+61): validate the TSecType checksum from 004746DD
// without calling its throwing native accessor or changing game state.
static bool DecodeHp(const BYTE* bytes, int& hp) {
    DWORD checksum = 0xBAADF00D, expected;
    unsigned short value = 0;
    for (int i = 0; i < 2; ++i) {
        const DWORD mixed = checksum ^ bytes[i];
        checksum = ((mixed >> 5) | (mixed << 27)) + bytes[i + 2];
        value |= static_cast<unsigned short>((bytes[i] ^ bytes[i + 2]) << (8 * i));
    }
    memcpy(&expected, bytes + 4, sizeof(expected));
    if (checksum != expected) return false;
    hp = static_cast<short>(value);
    return hp >= 0;
}
static Snapshot Read() {
    Snapshot s;
    __try {
        s.user = *reinterpret_cast<void**>(Native(0x00BEBF98));
        s.context = *reinterpret_cast<void**>(Native(0x00BE7918));
        s.dialog = *reinterpret_cast<void**>(Native(0x00BF1028));
        const auto app = *reinterpret_cast<const BYTE**>(Native(0x00BE7B38));
        if (app) s.tick = *reinterpret_cast<const DWORD*>(app + 0x18);
        if (s.user) {
            const auto u = static_cast<const BYTE*>(s.user);
            s.stance = *reinterpret_cast<const int*>(u + 0x570);
            s.effect = *reinterpret_cast<const DWORD*>(u + 0x1F9C);
        }
        if (s.context) {
            const auto c = static_cast<const BYTE*>(s.context);
            s.pending = *reinterpret_cast<const DWORD*>(c + 0x3520);
            s.character = *reinterpret_cast<void* const*>(c + 0x20B8);
        }
        s.valid = app && s.user && s.character &&
            DecodeHp(static_cast<const BYTE*>(s.character) + 0x61, s.hp);
    } __except (EXCEPTION_EXECUTE_HANDLER) { s.valid = false; }
    return s;
}
static bool Dead(int stance) { return (stance & ~1) == 18; }
static bool IsDead(const Snapshot& s) { return s.valid && s.hp == 0 && Dead(s.stance) && s.effect; }
struct Cycle {
    void* user = nullptr;
    void* context = nullptr;
    void* character = nullptr;
    DWORD id = 0, started = 0, missingSince = 0;
    bool active = false, missing = false, repaired = false;
    bool createAttempted = false, closed = false, timeoutLogged = false, readFailureLogged = false;
};
static thread_local Cycle cycle;
static thread_local DWORD nextCycle = 0, lastSample = 0;
struct LogBudget { DWORD started = 0; unsigned routine = 0, anomaly = 0; };
static thread_local LogBudget budget;
static bool AllowLog(bool anomaly) {
    if (!logging) return false;
    const DWORD now = GetTickCount();
    if (now - budget.started >= 3600000) { budget = LogBudget{}; budget.started = now; }
    // Normal deaths cannot exhaust the separate failure allowance. Limits reset
    // hourly instead of disabling diagnostics for the rest of a long session.
    auto& count = anomaly ? budget.anomaly : budget.routine;
    const unsigned limit = anomaly ? 64u : 256u;
    if (count >= limit) return false;
    ++count;
    return true;
}
static void Record(const char* phase, const Snapshot& s, bool anomaly = false,
                   int requested = -1, int force = -1, void* caller = nullptr,
                   void* origin = nullptr, DWORD exception = 0, void* fault = nullptr) {
    const DWORD error = GetLastError();
    if (AllowLog(anomaly)) {
        char stack[160]{};
        if (anomaly) {
            void* frames[12]{};
            const USHORT count = CaptureStackBackTrace(1, ARRAYSIZE(frames), frames, nullptr);
            for (USHORT i = 0; i < count; ++i)
                sprintf_s(stack + i * 9, sizeof(stack) - i * 9, "%08lX;", reinterpret_cast<DWORD>(frames[i]));
        }
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=death_dialog phase=%s cycle=%lu valid=%d hp=%d user=%p character=%p context=%p stance=%d requested=%d force=%d tick=%lu pending=%lu age=%ld dialog=%p effect=%08lX repaired=%d attempted=%d closed=%d caller=%p origin=%p exception=%08lX fault=%p stack=%s",
            phase, cycle.id, s.valid, s.hp, s.user, s.character, s.context, s.stance, requested, force,
            s.tick, s.pending, s.pending ? static_cast<LONG>(s.tick - s.pending) : -1L,
            s.dialog, s.effect, cycle.repaired, cycle.createAttempted, cycle.closed,
            caller, origin, exception, fault, stack);
    }
    SetLastError(error);
}
static bool SameCycle(const Snapshot& s) {
    return cycle.active && s.user == cycle.user && s.context == cycle.context && s.character == cycle.character;
}
static void Begin(const Snapshot& s, DWORD now) {
    cycle = Cycle{};
    cycle.user = s.user; cycle.context = s.context; cycle.character = s.character;
    cycle.id = ++nextCycle; cycle.started = now; cycle.active = true;
    cycle.createAttempted = s.dialog != nullptr;
}
static LONG NativeException(const char* phase, EXCEPTION_POINTERS* e) {
    Record(phase, Read(), true, -1, -1, nullptr, nullptr,
        e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_CONTINUE_SEARCH;
}
static void* ActionOrigin(void* caller, void* const* returnSlot) {
    // Verified CUserLocal wrapper saves ESI and pushes two arguments, so the
    // outer caller is returnSlot[4]. This works even with native FPO frames.
    if (caller != reinterpret_cast<void*>(Native(0x0094A11A))) return caller;
    __try { return returnSlot[4]; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
static void __fastcall ActionHook(void* avatar, void*, int requested, int force) {
    bool relevant = false;
    __try {
        const auto user = *reinterpret_cast<BYTE**>(Native(0x00BEBF98));
        if (user && avatar == user + 0x88) {
            const int previous = *reinterpret_cast<const int*>(user + 0x570);
            relevant = Dead(previous) != Dead(requested) || (Dead(requested) && force != 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!relevant) { action(avatar, requested, force); return; }
    const Snapshot before = Read();
    const bool entering = !Dead(before.stance) && Dead(requested);
    if (entering && before.valid && !SameCycle(before)) Begin(before, GetTickCount());
    // Repeated forced refreshes of an existing death preserve its timer and
    // need no log line. Always forward the original arguments unchanged.
    void* caller = _ReturnAddress();
    void* origin = ActionOrigin(caller, static_cast<void* const*>(_AddressOfReturnAddress()));
    if (entering || !Dead(requested)) Record("action_before", before, entering && force != 0, requested, force, caller, origin);
    __try { action(avatar, requested, force); }
    __except (NativeException("action_exception", GetExceptionInformation())) {}
    const Snapshot after = Read();
    if (entering || !Dead(requested)) Record("action_after", after, false, requested, force, caller, origin);
    if (after.valid && after.hp > 0) cycle = Cycle{};
}
static void Observe(const Snapshot& s, DWORD now) {
    if (!IsDead(s)) {
        if (cycle.active && !s.valid && !cycle.readFailureLogged) {
            cycle.readFailureLogged = true;
            Record("state_unavailable", s, true);
        }
        // A failed read must not forget that the player already chose revive.
        if (s.valid && (s.hp > 0 || !SameCycle(s))) cycle = Cycle{};
        cycle.missing = false;
        return;
    }
    if (!SameCycle(s)) { Begin(s, now); Record("observed_dead", s); }
    if (s.dialog) cycle.createAttempted = true;
    if (s.pending || s.dialog) cycle.missing = false;
    else if (!cycle.missing) { cycle.missing = true; cycle.missingSince = now; }
    if (recovery && cycle.missing && now - cycle.missingSince >= 3000 &&
        !cycle.repaired && !cycle.createAttempted && !cycle.closed && s.tick != 0) {
        cycle.repaired = true;
        Record("recover_missing_timer", s, true);
        // One attempt per death on the game's update thread. The native update
        // chooses and opens the normal/soul-stone UI after its 2200 ms delay.
        arm(s.context);
        Record("recover_scheduled", Read(), true);
    }
    if (!s.dialog && !cycle.closed && !cycle.timeoutLogged && now - cycle.started >= 10000) {
        cycle.timeoutLogged = true;
        Record("timeout", Read(), true);
    }
}
static void __fastcall UpdateHook(void* context, void*) {
    update(context);
    const DWORD now = GetTickCount();
    if (now - lastSample < 500) return;
    lastSample = now;
    const Snapshot s = Read();
    if (s.context == context) Observe(s, now);
}
static void* __cdecl CreateHook() {
    const Snapshot before = Read();
    if (!SameCycle(before) && IsDead(before)) Begin(before, GetTickCount());
    if (SameCycle(before)) cycle.createAttempted = true;
    Record("create_before", before, false, -1, -1, _ReturnAddress());
    void* result = nullptr;
    __try { result = create(); }
    __except (NativeException("create_exception", GetExceptionInformation())) {}
    const Snapshot after = Read();
    Record(result && after.dialog ? "create_after" : "create_failed", after, !result || !after.dialog);
    return result;
}
static void MarkCancel(const Snapshot& before, void* caller) {
    // 89825A: a revive choice; A03EC3: context reset / field change.
    if (before.dialog || cycle.createAttempted || caller == reinterpret_cast<void*>(Native(0x0089825A)) ||
        caller == reinterpret_cast<void*>(Native(0x00A03EC3))) cycle.closed = true;
}
static void __fastcall CancelHook(void* context, void*) {
    const Snapshot before = Read();
    void* caller = _ReturnAddress();
    // Reset may already have cleared the local-user pointer by this point.
    const bool tracked = cycle.active && context == cycle.context;
    if (tracked) {
        MarkCancel(before, caller);
        Record("cancel_before", before, before.pending != 0 && !before.dialog, -1, -1, caller);
    }
    cancel(context);
    if (tracked) Record("cancel_after", Read(), false, -1, -1, caller);
}
static bool Install(const wchar_t* configOverride = nullptr) {
    if (installed) return true;
    wchar_t path[MAX_PATH]{};
    if (!configOverride) {
        const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        if (!length || length >= ARRAYSIZE(path)) return false;
        wchar_t* slash = wcsrchr(path, L'\\');
        if (!slash || wcscpy_s(slash + 1, ARRAYSIZE(path) - (slash + 1 - path), L"config.ini")) return false;
        configOverride = path;
    }
    recovery = GetPrivateProfileIntW(L"DeathDialog", L"Recovery", 1, configOverride) != 0;
    logging = GetPrivateProfileIntW(L"DeathDialog", L"Logging", 1, configOverride) != 0;
    // DeathDialogTest/Mode is deliberately no longer read: injection removed.
    if (!recovery && !logging) return true;
    const BYTE actionBytes[] = {0x8B,0x44,0x24,0x04,0x53,0x56,0x57};
    const BYTE updateBytes[] = {0xB8,0xE0,0x93,0xAE,0x00};
    const BYTE createBytes[] = {0xB8,0x12,0xB4,0xAE,0x00};
    const BYTE cancelBytes[] = {0x83,0xA1,0x20,0x35,0x00,0x00,0x00};
    const BYTE armBytes[] = {0x83,0xB9,0x20,0x35,0,0,0,0x56,0x8D,0xB1,0x20,0x35,0,0,0x75,7,0xE8,0x4F,0x0B,0xF8,0xFF,0x89,6,0x5E,0xC3};
    const BYTE wrapperBytes[] = {0x56,0xFF,0x74,0x24,0x0C,0x8B,0xF1,0xFF,0x74,0x24,0x0C,0xE8,0xB7,0x4B,0xFE,0xFF};
    if (memcmp(reinterpret_cast<void*>(action), actionBytes, sizeof(actionBytes)) ||
        memcmp(reinterpret_cast<void*>(update), updateBytes, sizeof(updateBytes)) ||
        memcmp(reinterpret_cast<void*>(create), createBytes, sizeof(createBytes)) ||
        memcmp(reinterpret_cast<void*>(cancel), cancelBytes, sizeof(cancelBytes)) ||
        memcmp(reinterpret_cast<void*>(Native(0x00A066F3)), armBytes, sizeof(armBytes)) ||
        memcmp(reinterpret_cast<void*>(Native(0x0094A10A)), wrapperBytes, sizeof(wrapperBytes))) {
        ClientLog::Append(ClientLog::Component::Lifecycle, "event=death_dialog_install status=signature_mismatch");
        return false;
    }
    const bool a = Memory::SetHook(true, reinterpret_cast<void**>(&action), ActionHook);
    const bool b = a && Memory::SetHook(true, reinterpret_cast<void**>(&update), UpdateHook);
    const bool c = b && Memory::SetHook(true, reinterpret_cast<void**>(&create), CreateHook);
    const bool d = c && Memory::SetHook(true, reinterpret_cast<void**>(&cancel), CancelHook);
    if (!d) {
        if (c) Memory::SetHook(false, reinterpret_cast<void**>(&create), CreateHook);
        if (b) Memory::SetHook(false, reinterpret_cast<void**>(&update), UpdateHook);
        if (a) Memory::SetHook(false, reinterpret_cast<void**>(&action), ActionHook);
        ClientLog::Append(ClientLog::Component::Lifecycle, "event=death_dialog_install status=hook_failed");
        return false;
    }
    installed = true;
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "event=death_dialog_install status=ok version=2 recovery=%d logging=%d sampleMs=500 missingMs=3000 maxRoutinePerHour=256 maxAnomalyPerHour=64 testInjection=removed",
        recovery, logging);
    return true;
}
}
