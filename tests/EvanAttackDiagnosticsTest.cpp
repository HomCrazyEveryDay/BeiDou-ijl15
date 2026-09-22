#include <windows.h>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include "../ezorsia/EvanAttackDiagnostics.h"

static std::vector<std::string> logs;
void ClientLog::Append(Component, const char* format, ...) {
    char buffer[2304]; va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    logs.emplace_back(buffer);
}
static void Write(std::vector<unsigned char>& p, unsigned offset, int n) {
    for (unsigned i=0; i<4; ++i) p[offset+i] = static_cast<unsigned>(n) >> (i*8);
}
static std::vector<unsigned char> Fixture(bool incoming, bool charged) {
    unsigned header = incoming ? 26 : (charged ? 29 : 25);
    unsigned stride = incoming ? 13 : 30; // eight targets, two lines
    std::vector<unsigned char> p(header + 8*stride + (incoming && charged ? 4 : 0));
    p[incoming ? 4 : 0] = incoming ? 0xbc : 0x2e;
    p[incoming ? 10 : 3] = 0x82;
    if(incoming) { p[12]=30; Write(p,6,1234); }
    Write(p,incoming ? 13 : 4,charged ? 22121000 : 22181002);
    for(unsigned i=0; i<8; ++i) {
        unsigned offset=header+i*stride; Write(p,offset,100+i);
        Write(p,offset+(incoming ? 5 : 18),11372);
        Write(p,offset+(incoming ? 5 : 18)+4,17005-INT_MAX);
    }
    return p;
}
static bool Contains(const char* text) {
    for(const auto& s:logs) if(s.find(text)!=std::string::npos) return true;
    return false;
}
int main() {
    SetEnvironmentVariableA("BEIDOU_EVAN_ATTACK_LOG","1");
    using namespace EvanAttackDiagnostics;
    // Native transport prefix + opcode + 128-bit mask + beacon-only payload.
    std::vector<unsigned char> beacon(44); beacon[4]=0x20; beacon[12]=0x80;
    Write(beacon,24,1); Write(beacon,28,22151002); Write(beacon,37,70001);
    Beacon()={}; logs.clear();
    for(unsigned n=0;n<beacon.size();++n) Observe(beacon.data(),n,true);
    assert(Beacon().target==0);
    Observe(beacon.data(),44,true); assert(Beacon().target==70001);
    TrackBeacon(FindMob(70001),70001);
    Write(beacon,37,70002); Observe(beacon.data(),44,true);
    assert(Beacon().target==70002 && Contains("previous=70001 target=70002"));
    BeaconRetention(FindMob(70001),22151002,1);
    assert(Contains("oid=70001 receivedTarget=70002 retainedByNative=1"));
    auto count=logs.size();
    for(int i=0;i<1000;++i) BeaconRetention(FindMob(70001),22151002,1);
    assert(logs.size()==count);
    beacon[4]=0x21; Observe(beacon.data(),22,true); assert(Beacon().target==0);
    beacon[4]=0x20; beacon[6]=1; Observe(beacon.data(),44,true); assert(Beacon().target==0);
    for(int i=0;i<200;++i) Observe(beacon.data(),44,true);
    assert(Beacon().records==128);
    for(bool incoming : {false,true}) for(bool charged : {false,true}) {
        auto p=Fixture(incoming,charged); Current()={}; logs.clear();
        Observe(p.data(),static_cast<unsigned long>(p.size()),incoming);
        assert(Current().count==8 && Current().targets[7].oid==107);
        assert(Contains("valid=1") && Contains("raw=11372 signBit=0") && Contains("signBit=1"));
        RenderCall(FindMob(107),17005,1,1,true);
        assert(Contains("oid=107 damage=17005 line=1 criticalArg=1 forwarded=1"));
        // Every truncated prefix must be rejected before accessing target data.
        for(unsigned n=0; n<p.size(); ++n) {
            Current()={}; logs.clear(); Observe(p.data(),n,incoming);
            assert(Current().count==0);
        }
    }
    auto p=Fixture(false,false); Current()={}; logs.clear();
    RenderCall(FindMob(100),11372,0,0,true);
    Observe(p.data(),static_cast<unsigned long>(p.size()),false);
    assert(Contains("phase=before_send"));
    Current()={}; logs.clear();
    for(int i=0;i<100;++i) Observe(p.data(),static_cast<unsigned long>(p.size()),false);
    assert(Current().casts==8 && !Current().active);
    Write(p,4,22161001);
    Observe(p.data(),static_cast<unsigned long>(p.size()),false);
    assert(Current().casts==9 && Current().active && Current().skill==22161001);
    for(int i=0;i<10000;++i) RenderCall(FindMob(100),11372,0,0,true);
    assert(Current().records==8192 && Contains("event=evan_attack_limit"));
    puts("PASS: eight targets, critical flags, charged packets, truncated buffers, local render history, limits");
}
