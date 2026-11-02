#include "../ezorsia/VisitorsMovementDiagnostics.cpp"
#include <cstdlib>
#include <string>

namespace {
std::string output;
int moveCalls = 0, updateCalls = 0;
unsigned char mob[0x540]{}, mobTemplate[0x40]{}, vectorData[0x280]{}, localVector[0x280]{};
void Require(bool value, const char* reason) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",reason); std::exit(1); }
}
template<class T> void Put(void* p, size_t off, T value) {
    std::memcpy(static_cast<unsigned char*>(p)+off,&value,sizeof(value));
}
int __cdecl PlainInt(const void* p,unsigned) { return At<int>(p,0); }
double __cdecl PlainDouble(const void* p,unsigned) { return At<double>(p,0); }
void __fastcall NativeMove(void*,void*,InPacket* p) { ++moveCalls; p->offset += 26; }
void __fastcall NativeUpdate(void*,void*) { ++updateCalls; Put(vectorData,0x20,At<double>(vectorData,0x20)-10); }
void Reset() {
    used=0;enabled=true;startedCapture=false;trackedMob=nullptr;trackedOid=0;output.clear();
    readInt=readId=PlainInt;readDouble=PlainDouble;originalMove=NativeMove;originalUpdate=NativeUpdate;
    Put(mob,0x188,mobTemplate);Put(mobTemplate,0xC,9420076);Put(mob,0x17C,1234);
    Put(mob,0x118,vectorData+12);Put(mob,0x11C,localVector+12);
    Put(vectorData,0x20,100.0);Put(vectorData,0x38,-70.0);
}
}
bool Memory::SetHook(bool,void**,void*) { return false; }
HANDLE ClientLog::Open(Component) { return INVALID_HANDLE_VALUE; }
void ClientLog::Write(HANDLE,const char* s) { output+=s; }
void ClientLog::Append(Component,const char*,...) {}

int main() {
    unsigned char bytes[40]{};
    auto* p=bytes+4;p[2]=255;p[11]=1;p[12]=0;
    Put(p,7,short(-400));Put(p,9,short(200));Put(p,13,short(-386));Put(p,15,short(200));
    Put(p,17,short(55));Put(p,19,short(0));Put(p,21,short(17));p[23]=2;Put(p,24,short(250));
    InPacket packet{};packet.data=bytes;packet.offset=4;packet.length=30;
    auto wire=ReadWire(&packet);
    Require(wire.valid && wire.activity==-1 && wire.startX==-400 && wire.endX==-386
        && wire.vx==55 && wire.fh==17 && wire.duration==250,"decode actual escort packet layout");
    Require(packet.offset==4,"diagnostic must not consume packet");
    for (unsigned n=0;n<30;++n) { packet.length=static_cast<unsigned short>(n);Require(!ReadWire(&packet).valid,"reject truncation"); }
    packet.length=30;p[11]=2;Require(!ReadWire(&packet).valid,"do not guess other movement formats");p[11]=1;
    Reset();enabled=false;Move(mob,nullptr,&packet);Update(mob,nullptr);
    Require(moveCalls==1 && updateCalls==1 && used==0,"disabled hooks preserve native calls");
    Reset();packet.offset=4;Move(mob,nullptr,&packet);Update(mob,nullptr);
    Require(used==3 && rows[0].event=='B' && rows[1].event=='A' && rows[2].event=='U',"capture receive and update separately");
    Require(rows[0].wire.valid && rows[0].wire.duration==250 && rows[2].x==90,"capture real native position change");
    Require(packet.offset==30 && moveCalls==2 && updateCalls==2,"native packet and update behavior preserved");
    Put(mob,0x17C,1235);Capture(mob,'U');Require(used==3,"ignore reused mob pointer with different object id");
    Reset();Put(mobTemplate,0xC,100100);packet.offset=4;Move(mob,nullptr,&packet);
    Require(used==0 && !startedCapture,"ordinary monsters must not start capture");
    Reset();Capture(nullptr,'U');Require(used==0,"invalid diagnostic pointer does not crash");
    packet.offset=4;Move(mob,nullptr,&packet);
    for(size_t n=used;n<=capacity;++n)Capture(mob,'U');
    Require(used==capacity && !enabled,"bounded capture stops at capacity");
    Require(output.find("visitors_movement_end")!=std::string::npos,"complete captured output");
    Reset();packet.offset=4;Move(mob,nullptr,&packet);started=GetTickCount()-captureMillis;Capture(mob,'U');
    Require(!enabled && used==2,"capture stops after 15 seconds");
    std::puts("Visitors movement diagnostics: packet bounds, native forwarding, target filtering and capture limits passed");
}
