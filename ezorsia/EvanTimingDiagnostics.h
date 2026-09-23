#pragma once
#include "ClientLog.h"
#include "SnailTimingDiagnostics.h"
#include "ReactorTimingDiagnostics.h"

// Event-only diagnostics. No packet contents, frame traces, or process scanning.
// Thread-local state follows the client's game thread; update observes only the
// exact user passed to the native skill entry, never a saved pointer on a timer.
namespace EvanTimingDiagnostics {
using Attack = int(__thiscall*)(void*, void*, int, int, int);
using Buff = int(__thiscall*)(void*, void*, int, int, int, int, int, int);
using Prepare = void(__thiscall*)(void*, int, int, int);
using Update = void(__thiscall*)(void*);
using Action = int(__thiscall*)(void*);
static Attack attack = reinterpret_cast<Attack>(0x95571f);
static Buff buff = reinterpret_cast<Buff>(0x96d399);
static Prepare prepare = reinterpret_cast<Prepare>(0x453ad1);
static Update update = reinterpret_cast<Update>(0x930b27);
static Action action = reinterpret_cast<Action>(0x451b6a);
static volatile LONG records = 0;
static volatile LONG sequence = 0;
struct Cast {
    void* user = nullptr;
    int skill = 0;
    LONG id = 0;
    DWORD started = 0;
    bool pending = false;
};
static thread_local Cast current;
static thread_local Cast* entering = nullptr;
static thread_local DWORD lastAttempt = 0;
static thread_local int lastSkill = 0;
static thread_local unsigned suppressed = 0;
struct EntryScope {
    Cast* previous;
    explicit EntryScope(Cast& cast) : previous(entering) { entering = &cast; }
    ~EntryScope() { entering = previous; }
};

static int Read(void* object, unsigned offset) {
    return *reinterpret_cast<int*>(static_cast<unsigned char*>(object) + offset);
}
static bool Evan(void* skill) {
    return skill && Read(skill, 0) / 1000000 == 22;
}
static int Duration(void* avatar) {
    // Native CAvatar has separate normal and one-time action banks.
    return Read(avatar, 0x4f8 + (action(avatar) >= 0 ? 0x5dc : 0));
}
static bool Sample(void* user, void* skill) {
    const int id=Read(skill,0);
    const DWORD now=GetTickCount();
    void* avatar=static_cast<unsigned char*>(user)+0x88;
    // Throttle repeated attempts only while a one-time action is active.
    // Always observe the next unlocked attempt. Never suppress the game call.
    if(lastSkill==id && now-lastAttempt<250 && action(avatar)>=0) {
        ++suppressed;
        return false;
    }
    lastSkill=id;
    lastAttempt=now;
    return true;
}
static void Record(const Cast& cast, const char* phase, int a, int b, int c) {
    const DWORD error = GetLastError();
    const LONG n = InterlockedIncrement(&records);
    if (n <= 512) {
        ClientLog::Append(ClientLog::Component::Trace,
            "event=evan_timing cast=%ld skill=%d phase=%s elapsedMs=%lu a=%d b=%d c=%d",
            cast.id, cast.skill, phase, GetTickCount()-cast.started, a, b, c);
    } else if (n == 513) {
        ClientLog::Append(ClientLog::Component::Trace, "event=evan_timing_limit maxRecords=512");
    }
    SetLastError(error);
}
static Cast Begin(void* user, void* skill) {
    Cast cast{user, Read(skill, 0), InterlockedIncrement(&sequence), GetTickCount(), false};
    void* avatar = static_cast<unsigned char*>(user) + 0x88;
    if(suppressed) { Record(cast,"repeated_attempts",suppressed,0,0); suppressed=0; }
    Record(cast, "skill_entry", action(avatar), Duration(avatar), Read(user, 0x574));
    return cast;
}
static void End(Cast& cast, int result) {
    void* avatar = static_cast<unsigned char*>(cast.user) + 0x88;
    const int state = action(avatar);
    Record(cast, "skill_return", result, state, Duration(avatar));
    if (result && state >= 0) {
        if (current.pending) Record(current, "superseded", cast.skill, state, 0);
        cast.pending = true;
        current = cast;
    }
}
static int __fastcall AttackHook(void* user, void*, void* skill, int level, int x, int y) {
    if (skill && SnailTimingDiagnostics::Skill(Read(skill,0))) {
        const bool tracked=SnailTimingDiagnostics::Begin(Read(skill,0),"magic_entry");
        SnailTimingDiagnostics::Current().entering=tracked;
        const DWORD start=GetTickCount();
        int result=attack(user,skill,level,x,y);
        SnailTimingDiagnostics::Current().entering=false;
        if(tracked)SnailTimingDiagnostics::Log("magic_return",GetTickCount()-start,result);
        return result;
    }
    if (!Evan(skill) || records >= 512 || !Sample(user,skill)) return attack(user, skill, level, x, y);
    Cast cast = Begin(user, skill);
    int result;
    { EntryScope scope(cast); result = attack(user, skill, level, x, y); }
    End(cast, result);
    return result;
}
static int __fastcall BuffHook(void* user, void*, void* skill, int level, int a, int b, int c, int d, int e) {
    if (!Evan(skill) || records >= 512 || !Sample(user,skill)) return buff(user, skill, level, a, b, c, d, e);
    Cast cast = Begin(user, skill);
    int result;
    { EntryScope scope(cast); result = buff(user, skill, level, a, b, c, d, e); }
    End(cast, result);
    return result;
}
static void __fastcall PrepareHook(void* avatar, void*, int speed, int movementSpeed, int flag) {
    Cast* cast = entering;
    const bool tracked = cast && avatar == static_cast<unsigned char*>(cast->user) + 0x88;
    if (tracked) Record(*cast, "prepare_begin", speed, action(avatar), Duration(avatar));
    prepare(avatar, speed, movementSpeed, flag);
    if (tracked) Record(*cast, "prepare_end", speed, action(avatar), Duration(avatar));
}
static void __fastcall UpdateHook(void* user, void*) {
    const DWORD snailStart=GetTickCount();
    ReactorTimingDiagnostics::UpdateBegin(snailStart);
    SnailTimingDiagnostics::UpdateBegin(snailStart);
    update(user);
    ReactorTimingDiagnostics::UpdateEnd(snailStart);
    SnailTimingDiagnostics::UpdateEnd(snailStart);
    if (!current.pending || current.user != user) return;
    void* avatar = static_cast<unsigned char*>(user) + 0x88;
    const int state = action(avatar);
    if (state < 0 || GetTickCount()-current.started > 15000) {
        Record(current, state < 0 ? "action_clear_observed" : "observation_timeout",
            state, Read(avatar, 0x4f8), Read(user, 0x574));
        current.pending = false;
    }
}
static void HitDelay(int previous, int duration, int speed, int negative, int total) {
    if (entering) Record(*entering, "illusion_timing", previous, duration, speed);
    if (entering && total > 0)
        Record(*entering, "illusion_hit_schedule", negative, total,
            static_cast<int>(static_cast<long long>(duration) * negative / total));
}
static void QueuedHit(int index, int offset, int remaining) {
    if (entering && entering->skill == 22171002)
        Record(*entering, "illusion_hit_queued", index, offset, remaining);
}
static void Install() {
    char disabled[8]{};
    GetEnvironmentVariableA("BEIDOU_EVAN_TIMING_LOG", disabled, sizeof(disabled));
    if (disabled[0] == '0') return;
    // Native entry signatures; never attach to a different client build.
    const unsigned char a[]={0xb8,0xd3,0xe8,0xad,0};
    const unsigned char b[]={0xb8,0xc8,0xfb,0xad,0};
    const unsigned char p[]={0xb8,0x1a,0xc5,0xa7,0};
    if (memcmp(reinterpret_cast<void*>(attack),a,5) || memcmp(reinterpret_cast<void*>(buff),b,5)
        || memcmp(reinterpret_cast<void*>(prepare),p,5)) {
        ClientLog::Append(ClientLog::Component::Trace,"event=evan_timing_install status=signature_mismatch");
        return;
    }
    const bool h1=Memory::SetHook(true,reinterpret_cast<void**>(&attack),AttackHook);
    const bool h2=h1 && Memory::SetHook(true,reinterpret_cast<void**>(&buff),BuffHook);
    const bool h3=h2 && Memory::SetHook(true,reinterpret_cast<void**>(&prepare),PrepareHook);
    const bool h4=h3 && Memory::SetHook(true,reinterpret_cast<void**>(&update),UpdateHook);
    if (!h4) {
        if(h3) Memory::SetHook(false,reinterpret_cast<void**>(&prepare),PrepareHook);
        if(h2) Memory::SetHook(false,reinterpret_cast<void**>(&buff),BuffHook);
        if(h1) Memory::SetHook(false,reinterpret_cast<void**>(&attack),AttackHook);
    }
    ClientLog::Append(ClientLog::Component::Trace,"event=evan_timing_install status=%s maxRecords=512",h4?"ok":"failed");
}
}
