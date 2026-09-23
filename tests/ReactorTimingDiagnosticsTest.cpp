#include "../ezorsia/ReactorTimingDiagnostics.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

static std::vector<std::string> lines;
void ClientLog::Append(Component, const char* format, ...) {
    char buffer[1024]; va_list args; va_start(args,format);
    vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);lines.emplace_back(buffer);
}
int main() {
    using namespace ReactorTimingDiagnostics;
    unsigned char outgoing[20]={0xcd,0,0x34,0x12};
    unsigned char incoming[11]={0,0,0,0,0x15,1,0x34,0x12,0,0,1};
    assert(!Send(nullptr,20));assert(!Send(outgoing,19));
    assert(!Receive(incoming,11));
    outgoing[0]=0x2c;assert(!Send(outgoing,20));outgoing[0]=0xcd;
    assert(Send(outgoing,20));assert(Current().oid==0x1234);
    assert(Receive(incoming,11));assert(!Receive(incoming,10));
    incoming[6]=0x35;assert(!Receive(incoming,11));incoming[6]=0x34;
    const DWORD now=GetTickCount();Current().lastUpdate=now-500;
    UpdateBegin(now);assert(lines.back().find("update_gap")!=std::string::npos);
    Current().started=now-4000;UpdateEnd(now);
    assert(!Active());assert(!Receive(incoming,11));
    for(unsigned i=1;i<32;++i)assert(Send(outgoing,20));
    assert(!Send(outgoing,20));
    for(int i=0;i<400;++i)Log("test",0);
    assert(lines.size()==256);
    puts("PASS reactor packet bounds, opcode/OID correlation, update gap, expiry and record limits");
}
