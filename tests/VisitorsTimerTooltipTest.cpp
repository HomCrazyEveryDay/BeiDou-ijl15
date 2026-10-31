#include "../ezorsia/VisitorsTimerTooltip.cpp"
#include "VisitorsShuttleTest.h"
#include <string>
#include <thread>

namespace ClientLog { void Append(Component, const char*, ...) {} }
namespace {
using TestSupport::Require;
using TestSupport::Set;
ULONGLONG testTick = 100000;
ULONGLONG WINAPI Tick() { return testTick; }
constexpr ULONGLONG start = 134381952000000000ull;
BYTE window[0x700]{}, context[0x4000]{}, character[16]{}, otherCharacter[16]{};
BYTE item[0x30]{};
void* mouseMethods[16]{};
void* owner = window + 0x74;
std::string originalTitle, rendered;
int draws = 0, updates = 0, mouseMoves = 0, nameCalls = 0;
Arguments expected{};
bool forwardCheck = false;
bool slotEmpty = false;
int protectCalls = 0, failProtection = -1;
BOOL WINAPI Protect(void* address, SIZE_T size, DWORD protection, DWORD* previous) {
    if (++protectCalls == failProtection) return FALSE;
    return VirtualProtect(address, size, protection, previous);
}
std::string Gbk(const wchar_t* text) {
    char result[192]{};
    Require(WideCharToMultiByte(936,0,text,-1,result,sizeof(result),nullptr,nullptr)>0,"GBK text conversion");
    return result;
}
std::string TitleFor(int minute) {
    wchar_t text[96]{};
    swprintf_s(text,L"OSSS\u8ba1\u65f6\u5668\uff08\u5df2\u8fc7%d\u5206\u949f\uff09",minute);
    return Gbk(text);
}
void* __fastcall GetName(void*, void*, char** result, int) {
    ++nameCalls;
    *result = _strdup(originalTitle.c_str());
    return result;
}
void __fastcall Assign(char** result, void*, const char* text, int length) {
    Require(length==-1,"ZXString::Assign native ABI");
    std::free(*result);
    *result = _strdup(text);
}
int __fastcall ItemId(const int* value,void*) { return *value; }
int __fastcall Left(void* self,void*) { Require(self==window+4,"mouse interface coordinate this"); return 100; }
int __fastcall Top(void* self,void*) { Require(self==window+4,"mouse interface coordinate this"); return 200; }
void ClearNative() {
    VisitorsTimerTooltip::Clear(owner);
    Set<void*>(owner,0x10,nullptr);
}
void __fastcall ClearMock(void* self,void*) { Require(self==owner,"clear stale native tooltip"); ClearNative(); }
void __fastcall BuildNative(void* self,void*,Arguments args) {
    Require(self==owner,"tooltip owner passed through trampoline");
    if (forwardCheck) Require(!std::memcmp(&args,&expected,sizeof(args)),"all eight stack words forwarded unmodified");
    if (Layer(owner) && *reinterpret_cast<int*>(window+0x74+0x14)==static_cast<int>(args.values[0])
        && *reinterpret_cast<int*>(window+0x74+0x18)==static_cast<int>(args.values[1])) return;
    ClearNative(); // Native MakeLayer clears the old tooltip during a rebuild.
    ++draws;
    char* text = nullptr;
    const auto getter=reinterpret_cast<NameGetter>(Native(0x008EE57A));
    // CALL-patch harness: return after the patched call, like the real builder.
    getter(reinterpret_cast<void*>(1),&text,*reinterpret_cast<int*>(item+0x0c));
    Require(text!=nullptr,"name getter returns ZXString");
    rendered=text;
    std::free(text);
    Set<void*>(owner,0x10,reinterpret_cast<void*>(0x1000+draws));
    Set<int>(owner,0x14,args.values[0]);
    Set<int>(owner,0x18,args.values[1]);
}
void __fastcall NativeUpdate(void* self,void*) { Require(self==window,"CUIItem update this"); ++updates; }
int __fastcall MouseMove(void* self,void*,int x,int y) {
    Require(self==window+4 && x==30 && y==40,"refresh preserves relative cursor x/y, including native +20");
    ++mouseMoves;
    if (slotEmpty) return 0;
    Arguments args{{130,260,reinterpret_cast<DWORD>(item),0,0,0,0,0}};
    reinterpret_cast<Builder>(Native(0x008F5B20))(owner,args);
    return 0;
}
void Packet(ULONGLONG time) {
    unsigned char bytes[31]{};
    bytes[4]=0x7D;
    std::memcpy(bytes+23,&time,8);
    unsigned char before[31]; std::memcpy(before,bytes,31);
    VisitorsTimerTooltip::ObservePacket(bytes,31);
    Require(!std::memcmp(bytes,before,31),"observing server time never mutates incoming bytes");
}
void Open(int id=timerItem,ULONGLONG expiration=start+duration) {
    Set<int>(item,0x0c,id); Set<ULONGLONG>(item,0x20,expiration);
    expected={{130,260,reinterpret_cast<DWORD>(item),0,0,0,0,0}};
    reinterpret_cast<Builder>(Native(0x008F5B20))(owner,expected);
}
void TickUpdate() {
    reinterpret_cast<Update>(*reinterpret_cast<void**>(Native(0x00B39108)))(window);
}
void TimeTests() {
    Require(Minutes(start+duration,start)==0,"fresh timer starts at zero");
    for (int m=1;m<=60;++m) {
        Require(Minutes(start+duration,start+m*ticksPerMinute-1)==m-1,"no early minute increment");
        Require(Minutes(start+duration,start+m*ticksPerMinute)==m,"exact minute boundary");
    }
    Require(Minutes(start+duration,start+duration+ticksPerMinute)==60,"expired timer caps at sixty");
    Require(Minutes(start+duration+1,start)==0,"future item cannot produce negative minutes");
    Require(Minutes(0,start)==-1 && Minutes(~0ull,start)==-1 && Minutes(start,0)==-1,"invalid and permanent dates fall back");
}
}
int main(int argc,char** argv) {
    Require(argc==2,"original EXE path required");
    TimeTests();
    auto module=TestSupport::LoadImage(argv[1]);
    for (const auto& signature:signatures)
        Require(!std::memcmp(reinterpret_cast<void*>(signature.address),signature.bytes,signature.size),"original client ABI signature");
    Require(!VisitorsTimerTooltip::Install(nullptr),"reject unsupported executable");
    for (const auto& signature:signatures) {
        auto at=reinterpret_cast<BYTE*>(signature.address);
        const BYTE before=*at; *at^=1;
        Require(!VisitorsTimerTooltip::Install(module),"mismatched dependency refuses installation");
        *at=before;
    }
    protectMemory=Protect;
    for (int failure=1;failure<=7;++failure) {
        protectCalls=0; failProtection=failure;
        Require(!VisitorsTimerTooltip::Install(module),"memory-protection fault is reported");
        for (int i=0;i<3;++i) Require(!std::memcmp(reinterpret_cast<void*>(signatures[i].address),signatures[i].bytes,signatures[i].size),"failed install restores every patched byte");
    }
    failProtection=-1;
    Require(VisitorsTimerTooltip::Install(module),"install verified hooks");
    Require(VisitorsTimerTooltip::Install(module),"idempotent installation");
    MEMORY_BASIC_INFORMATION memory{};
    VirtualQuery(reinterpret_cast<void*>(originalBuilder),&memory,sizeof(memory));
    Require(memory.Protect==PAGE_EXECUTE_READ,"trampoline is executable and read-only");
    TestSupport::Jump(Native(0x008F5B25),BuildNative);
    TestSupport::Jump(Native(0x005CF63E),GetName);
    TestSupport::Jump(Native(0x00414617),Assign);
    TestSupport::Jump(Native(0x0042873D),ItemId);
    TestSupport::Jump(Native(0x0081E255),NativeUpdate);
    TestSupport::Jump(Native(0x0081D8AD),MouseMove);
    TestSupport::Jump(Native(0x008E6E23),ClearMock);
    // A thiscall name getter's two arguments must remain in place across the
    // actual patched CALL. Use a tail jump in this isolated harness; its target
    // must still be precisely the NameHook installed at the real callsite.
    auto site=reinterpret_cast<BYTE*>(Native(0x008EE57A));
    const auto destination=reinterpret_cast<DWORD>(site)+5+*reinterpret_cast<DWORD*>(site+1);
    Require(site[0]==0xE8 && destination==reinterpret_cast<DWORD>(NameHook),"name callsite installed correctly");
    site[0]=0xE9;
    tickCount=Tick;
    Set<void*>(reinterpret_cast<void*>(Native(0x00BED654)),0,window);
    Set<void*>(reinterpret_cast<void*>(Native(0x00BE7918)),0,context);
    Set<void*>(context,0x20b8,character);
    mouseMethods[0x2c/4]=Left; mouseMethods[0x30/4]=Top;
    Set<void*>(window,4,mouseMethods);
    originalTitle=Gbk(L"OSSS\u8ba1\u65f6\u5668\uff080\u5206\u949f\uff09");
    Open();
    Require(rendered==originalTitle && !hover.owner,"unknown server clock preserves native title");
    ClearNative(); Packet(start); forwardCheck=true; Open(); forwardCheck=false;
    Require(rendered==TitleFor(0) && hover.owner==owner,"fresh timer uses WZ name with elapsed suffix");
    const int initialDraws=draws;
    for (int i=0;i<240;++i) { testTick+=250; TickUpdate(); }
    Require(draws==initialDraws+1 && mouseMoves==1 && rendered==TitleFor(1),"stationary hover redraws exactly once per minute");
    Require(updates==240,"original inventory update is never suppressed");
    Open(); Require(draws==initialDraws+1,"same minute still uses native tooltip cache");
    testTick+=58*60000; TickUpdate();
    Require(rendered==TitleFor(59),"long idle advances directly to current minute");
    testTick+=60000; TickUpdate();
    Require(rendered==TitleFor(60) && *reinterpret_cast<int*>(item+0xc)==timerItem,"minute sixty never mutates item or grants completion");
    const int atSixty=draws; testTick+=60000; TickUpdate();
    Require(draws==atSixty,"no repeated redraw after sixty");
    ClearNative(); Open(4032736);
    Require(rendered==originalTitle && !hover.owner,"server-completed item remains entirely native");
    ClearNative(); Packet(start); Open();
    const int beforeClear=draws; ClearNative(); testTick+=60000; TickUpdate();
    Require(draws==beforeClear && !hover.owner,"closing tooltip cancels pending refresh");
    Packet(start); Open(); Set<void*>(context,0x20b8,otherCharacter); testTick+=60000; TickUpdate();
    Require(!hover.owner,"character switch clears old hover"); Set<void*>(context,0x20b8,character);
    ClearNative(); Packet(start); Open(); Set<void*>(reinterpret_cast<void*>(Native(0x00BED654)),0,nullptr); TickUpdate();
    Require(!hover.owner,"destroyed inventory clears state without dereferencing its owner");
    Set<void*>(reinterpret_cast<void*>(Native(0x00BED654)),0,window);
    ClearNative(); Packet(start); Open();
    unsigned char login[6]{}; VisitorsTimerTooltip::ObservePacket(login,sizeof(login)); testTick+=1000; TickUpdate();
    Require(!hover.owner && !Now(testTick).stamp,"login resets previous session server clock");
    ClearNative(); Packet(start); Open();
    const auto saved=hover;
    std::thread worker([]{ Refresh(window); }); worker.join();
    Require(hover.owner==saved.owner && hover.minutes==saved.minutes,"UI updates are confined to the owning thread");
    ClearNative(); Packet(start+12*ticksPerMinute); Open();
    Require(rendered==TitleFor(12),"map/channel timestamp restores elapsed time immediately");
    slotEmpty=true; testTick+=60000; TickUpdate();
    Require(!hover.owner && !Layer(owner),"empty slot removes old title and stops refresh attempts");
    slotEmpty=false; Packet(start); Open();
    const int beforeReplacement=draws;
    Open(4000000);
    Require(draws==beforeReplacement+1 && !hover.owner && rendered==originalTitle,"replacement item at same coordinates invalidates old timer title");
    ClearNative(); Open(timerItem,~0ull);
    Require(!hover.owner && rendered==originalTitle,"permanent GM timer falls back to original text");
    Packet(1); Require(!Now(testTick).stamp,"malformed time does not use client wall clock as fallback");
    std::printf("PASS: 120 minute boundaries; native hook ABI and rollback; stationary refresh; lifecycle; server-clock synchronization; no item writes\n");
    return 0;
}
