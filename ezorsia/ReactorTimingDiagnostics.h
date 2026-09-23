#pragma once
#include "ClientLog.h"

// Event-only observation of native reactor packets. No payloads or behavior changes.
namespace ReactorTimingDiagnostics {
inline bool Enabled() {
    static const bool enabled=[] { char value[8]{};
        GetEnvironmentVariableA("BEIDOU_REACTOR_TIMING_LOG",value,sizeof(value));
        return value[0]!='0'; }();
    return enabled;
}
struct State {
    unsigned records=0, hits=0, oid=0;
    DWORD started=0, lastUpdate=0;
    bool active=false;
};
inline State& Current() { static thread_local State state; return state; }
inline unsigned Read32(const unsigned char* p) {
    return unsigned(p[0]) | (unsigned(p[1])<<8) | (unsigned(p[2])<<16) | (unsigned(p[3])<<24);
}
inline void Log(const char* phase,DWORD duration,int detail=0) {
    auto& s=Current();
    if(!Enabled() || s.records>=256)return;
    const DWORD error=GetLastError(); ++s.records;
    ClientLog::Append(ClientLog::Component::Trace,
        "event=reactor_timing hit=%u oid=%u phase=%s durationMs=%lu sinceSendMs=%lu detail=%d tick=%lu",
        s.hits,s.oid,phase,duration,s.active?GetTickCount()-s.started:0,detail,GetTickCount());
    SetLastError(error);
}
inline bool Send(const unsigned char* data,unsigned size) {
    if(!Enabled() || !data || size<20 || data[0]!=0xcd || data[1]!=0)return false;
    auto& s=Current(); if(s.hits>=32 || s.records>=256)return false;
    ++s.hits;s.oid=Read32(data+2);s.started=GetTickCount();s.active=true;
    Log("send_begin",s.lastUpdate?s.started-s.lastUpdate:0,static_cast<int>(Read32(data+16)));
    return true;
}
inline bool Receive(const unsigned char* data,unsigned size) {
    if(!Enabled() || !data || size<11 || data[4]!=0x15 || data[5]!=1)return false;
    auto& s=Current();
    if(!s.active || s.records>=256 || Read32(data+6)!=s.oid)return false;
    Log("receive_begin",0,data[10]);return true;
}
inline void UpdateBegin(DWORD now) {
    if(!Enabled())return;
    auto& s=Current();
    if(s.active && s.lastUpdate && now-s.lastUpdate>=100)Log("update_gap",now-s.lastUpdate);
    s.lastUpdate=now;
}
inline void UpdateEnd(DWORD start) {
    auto& s=Current(); if(!Enabled() || !s.active)return;
    const DWORD now=GetTickCount();
    if(now-start>=100)Log("update_slow",now-start);
    if(now-s.started>=3000) {Log("window_end",now-s.started);s.active=false;}
}
inline bool Active() { return Enabled() && Current().active && Current().records<256; }
}
