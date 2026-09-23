#pragma once
#include "ClientLog.h"

// Bounded event diagnostics shared by the game-thread update and send hooks.
namespace SnailTimingDiagnostics {
inline bool Skill(int id) { return id==1000 || id==10001000 || id==20001000 || id==20011000; }
inline bool Enabled() {
    static const bool value=[] { char v[8]{};
        GetEnvironmentVariableA("BEIDOU_SNAIL_TIMING_LOG",v,sizeof(v)); return v[0]!='0'; }();
    return value;
}
struct State {
    unsigned casts=0,records=0;
    int skill=0;
    DWORD lastUpdate=0, started=0;
    bool active=false, entering=false;
};
inline State& Current() { static thread_local State value; return value; }
inline void Log(const char* phase,DWORD elapsed,int detail) {
    auto& s=Current();
    if(s.records>=256)return;
    const DWORD error=GetLastError(); ++s.records;
    ClientLog::Append(ClientLog::Component::Trace,
        "event=snail_timing cast=%u skill=%d phase=%s elapsedMs=%lu detail=%d tick=%lu",
        s.casts,s.skill,phase,elapsed,detail,GetTickCount());
    SetLastError(error);
}
inline bool Begin(int skill,const char* phase) {
    if(!Enabled() || !Skill(skill))return false;
    auto& s=Current(); if(s.casts>=32 || s.records>=256)return false;
    ++s.casts;s.skill=skill;s.started=GetTickCount();s.active=true;
    Log(phase,s.lastUpdate?s.started-s.lastUpdate:0,0);return true;
}
inline void Packet(const unsigned char* data,unsigned long size) {
    if(!Enabled() || !data || size<8 || data[1]!=0 ||
        (data[0]!=0x2c && data[0]!=0x2d && data[0]!=0x2e))return;
    unsigned id=0;for(unsigned i=0;i<4;++i)id|=static_cast<unsigned>(data[4+i])<<(8*i);
    if(!Skill(static_cast<int>(id)))return;
    auto& s=Current();
    // A native magic-entry observation may already own this same cast.
    if(!(s.entering && s.active && s.skill==static_cast<int>(id))) {
        if(!Begin(static_cast<int>(id),"send_observed"))return;
    }
    Log("send_begin",GetTickCount()-s.started,data[0]);
}
inline bool Active() {
    auto& s=Current();return Enabled() && s.active && s.records<256;
}
inline void UpdateBegin(DWORD now) {
    if(!Enabled())return;
    auto& s=Current();
    if(s.active && s.lastUpdate && now-s.lastUpdate>=100)Log("update_gap",now-s.lastUpdate,0);
    s.lastUpdate=now;
}
inline void UpdateEnd(DWORD started) {
    if(!Active())return;
    auto& s=Current();const DWORD now=GetTickCount();
    if(now-started>=100)Log("update_slow",now-started,0);
    if(now-s.started>=3000) {Log("window_end",now-s.started,0);s.active=false;}
}
}
