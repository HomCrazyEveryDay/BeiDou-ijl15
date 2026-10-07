#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "detours.h"
class Memory {public:static bool SetHook(bool,void**,void*);static void WriteInt(DWORD,unsigned int);static void WriteByte(DWORD,unsigned char);};
namespace CrashReporter {void RecordEvent(const char*,const char*,...) {}}
bool Memory::SetHook(bool attach,void** target,void* detour){
    if(DetourTransactionBegin()!=NO_ERROR)return false;
    if(DetourUpdateThread(GetCurrentThread())==NO_ERROR && (attach?DetourAttach:DetourDetach)(target,detour)==NO_ERROR && DetourTransactionCommit()==NO_ERROR)return true;
    DetourTransactionAbort();return false;
}
void Memory::WriteInt(DWORD address,unsigned int value){*reinterpret_cast<unsigned*>(address)=value;}
void Memory::WriteByte(DWORD address,unsigned char value){*reinterpret_cast<unsigned char*>(address)=value;}
#include "MonthlyShopUnderTest.cpp"

void Require(bool condition,const char* message){if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
void MapClient(const wchar_t* path){
    FILE* file=nullptr;Require(_wfopen_s(&file,path,L"rb")==0,"read supported executable without starting game");
    BYTE header[4096];Require(fread(header,1,sizeof(header),file)==sizeof(header),"PE header");
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(header+reinterpret_cast<IMAGE_DOS_HEADER*>(header)->e_lfanew);
    Require(nt->FileHeader.TimeDateStamp==0x4B7C15C9 && nt->OptionalHeader.SizeOfImage==0x00A94000 && nt->OptionalHeader.ImageBase==0x00400000,"supported 083 image");
    auto base=static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(0x30400000),nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Require(base==reinterpret_cast<void*>(0x30400000),"isolated native image mapping");memcpy(base,header,sizeof(header));
    auto section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){
        fseek(file,section[i].PointerToRawData,SEEK_SET);
        Require(fread(base+section[i].VirtualAddress,1,section[i].SizeOfRawData,file)==section[i].SizeOfRawData,"read section");
    }
    fclose(file);
    // The EXE has no relocation directory. Only relocate the callback table
    // entries checked by Install; all invoked game services are replaced below.
    for(DWORD address:{0xAFE11Cu,0xAFE13Cu,0xAFE110u,0xAFE120u,0xAFE124u,0xAF3014u,0xAF2FB4u,
        0xAF1C30u,0xAF1C24u,0xAF1C34u}){
        *reinterpret_cast<DWORD*>(address+0x30000000)+=0x30000000;
    }
}
void Jump(DWORD address,void* target){
    auto from=reinterpret_cast<BYTE*>(address);from[0]=0xe9;
    *reinterpret_cast<DWORD*>(from+1)=reinterpret_cast<DWORD>(target)-address-5;
    FlushInstructionCache(GetCurrentProcess(),from,5);
}
std::vector<std::vector<BYTE>> sentPackets;
int invalidations=0,updates=0,destroys=0,creates=0,clicks=0,soundReleases=0,tabCanvasReads=0;
void* expectedTab=nullptr;
void* __cdecl SoundPool(){return reinterpret_cast<void*>(43);}
DWORD* __fastcall SoundName(void* pool,void*,DWORD* out,int id){
    Require(pool==reinterpret_cast<void*>(43) && id==0x4cf,"same sound ID as native CCtrlTab");
    *out=reinterpret_cast<DWORD>("BtMouseClick");return out;
}
void __cdecl PlayClick(const char* name){Require(std::strcmp(name,"BtMouseClick")==0,"native UI sound ABI");++clicks;}
void __fastcall ReleaseSound(DWORD* name,void*){Require(*name!=0,"release native sound string");*name=0;++soundReleases;}
DWORD* __fastcall TabCanvas(void* self,void*,DWORD* out,int index){
    Require(self==expectedTab && index==0,"tabs use native control canvas, not window background");
    ++tabCanvasReads;*out=0;return out;
}
void __fastcall Capture(void* socket,void*,void* packet){
    auto words=static_cast<DWORD*>(packet);
    Require(socket==reinterpret_cast<void*>(42),"native socket passed in ECX");
    Require(words[0]==0 && words[3]==0 && words[4]==0,"plaintext COutPacket must enter native custom encryption");
    Require(words[2]>=2 && words[2]<=65535,"native packet length");
    auto data=reinterpret_cast<BYTE*>(words[1]);sentPackets.emplace_back(data,data+words[2]);
    // Native Encode writes its payload. The caller's reusable bytes must survive.
    data[0]^=0xff;words[3]=1;
}
void __fastcall Invalidated(void*,void*,const RECT*){++invalidations;}
void __fastcall Updated(void*,void*){++updates;}
void __fastcall Destroyed(void*,void*){++destroys;}
void __fastcall Created(void*,void*,void*){++creates;}
int __fastcall ItemId(void* item,void*){return *static_cast<int*>(item);}
void __fastcall Close(void* self,void*,int result){
    Require(result==0,"replace old shop without leave-shop request");
    NpcDestroy(self,nullptr);*reinterpret_cast<void**>(0x30BE7910)=nullptr;
}
Snapshot Quote(int channel,int month=202610,int token=12){
    Snapshot s;s.channel=channel;s.token=token;s.month=month;s.points=12345678901LL;
    s.months={{202610,1,"10月上新"},{202609,20,"9月上新"},{202608,20,"8月上新"},{-1,1,"全部"}};
    return s;
}
std::vector<BYTE> Reply(const Snapshot& s){
    using MonthlyShopModel::Put;
    std::vector<BYTE> bytes(4,0);Put(bytes,0x100f,2);Put(bytes,0x4d53,2);Put(bytes,1,1);Put(bytes,s.channel,1);
    Put(bytes,s.token,4);Put(bytes,s.month,4);Put(bytes,s.multiplier,4);
    Put(bytes,static_cast<unsigned>(s.points),4);Put(bytes,static_cast<unsigned>(s.points>>32),4);
    Put(bytes,s.changed,1);Put(bytes,static_cast<unsigned>(s.months.size()),1);
    for(const auto& month:s.months){Put(bytes,month.id,4);Put(bytes,month.multiplier,4);Put(bytes,static_cast<unsigned>(month.label.size()),2);bytes.insert(bytes.end(),month.label.begin(),month.label.end());}
    Put(bytes,0,2);return bytes;
}
void Receive(const Snapshot& s){
    auto bytes=Reply(s);Require(MonthlyShop::HandlePacket(bytes.data(),static_cast<unsigned short>(bytes.size())),"server reply accepted");
}
void ReplaceWindow(void* window){
    MonthlyShop::BeforeNativePacket(0x131);
    *reinterpret_cast<void**>(0x30BE7910)=window;NpcCreate(window,nullptr,nullptr);
}
void CheckSelect(int channel,int month,int token){
    auto& bytes=sentPackets.back();Require(bytes.size()==14,"exact select packet length");
    MonthlyShopModel::Reader r{bytes.data(),bytes.size()};
    Require(r.read(2)==0x1005 && r.read(2)==0x4d53 && r.read(1)==1,"select opcode, magic and version");
    Require(r.read(1)==channel && r.read(4)==token && static_cast<int>(r.read(4))==month,"select channel, token and month");
}
int wmain(int argc,wchar_t** argv){
    Require(argc==2,"client executable path");MapClient(argv[1]);
    const BYTE encryptedBranch[]={0x83,0x7b,0x0c,0,0x0f,0x85,0xc1,1,0,0};
    Require(std::memcmp(reinterpret_cast<void*>(0x306ecb52),encryptedBranch,sizeof(encryptedBranch))==0,"real Encode skips custom cipher when packet+0x0c is nonzero");
    Require(*reinterpret_cast<WORD*>(0x309e00ce)==0x50ff && *reinterpret_cast<BYTE*>(0x309e00d0)==0x10,"native DestroyWnd calls OnDestroy slot+0x10");
    Require(MonthlyShop::Install(),"all production hook guards match supported executable");
    Require(*reinterpret_cast<void**>(0x30AFE110)==reinterpret_cast<void*>(NpcUpdate),"timer installed on actual Update slot");
    Require(*reinterpret_cast<void**>(0x30AFE120)==reinterpret_cast<void*>(NpcDestroy),"actual destruction callback hooked");
    Require(*reinterpret_cast<DWORD*>(0x30AFE124)==0x309DEB57,"adjacent non-destruction callback remains native");
    Jump(0x3049637B,Capture);Jump(0x309E04C9,Invalidated);Jump(0x304244B1,Close);Jump(0x3042873D,ItemId);
    Jump(0x3079E805,SoundPool);Jump(0x30406276,SoundName);Jump(0x30989588,PlayClick);Jump(0x3040265E,ReleaseSound);Jump(0x304C0690,TabCanvas);
    npcUpdate=reinterpret_cast<VoidFn>(Updated);npcDestroy=reinterpret_cast<VoidFn>(Destroyed);npcCreate=reinterpret_cast<CreateFn>(Created);
    *reinterpret_cast<void**>(0x30BE7914)=reinterpret_cast<void*>(42);
    auto request=MonthlyShopModel::Select(Quote(1),202609),original=request;
    Require(Send(request) && request==original,"native payload mutation cannot change caller's bytes");CheckSelect(1,202609,12);

    BYTE window[0x120]{},tab[0x40]{};
    expectedTab=tab;
    *reinterpret_cast<void**>(window+0x9c)=tab;*reinterpret_cast<void**>(0x30BE7910)=window;
    Receive(Quote(1));NpcCreate(window,nullptr,nullptr);
    Require(npc.active && !npc.pending && creates==1 && ManagedBuyTab(tab),"first shop receives metadata before native creation");
    Require(Status(npc).empty(),"ordinary shop has no multiplier or price status");
    TabMouse(tab+4,nullptr,WM_LBUTTONDOWN,0,110,8);CheckSelect(1,202609,12);
    Require(npc.pending && npc.data.month==202610,"visible month changes only after server reply");
    auto sent=sentPackets.size();TabMouse(tab+4,nullptr,WM_LBUTTONDOWN,0,170,8);
    Require(sentPackets.size()==sent,"pending request suppresses repeated clicks");
    npc.sent=GetTickCount()-10001;int before=invalidations;
    reinterpret_cast<VoidFn>(*reinterpret_cast<void**>(0x30AFE110))(window);
    Require(!npc.pending && npc.retryNeeded && invalidations==before+1 && updates==1,"Update expires request and redraws even without Draw");
    TabMouse(tab+4,nullptr,WM_LBUTTONDOWN,0,110,8);
    Require(npc.pending && !npc.retryNeeded && sentPackets.size()==sent+1,"timeout allows retry");
    Receive(Quote(1,202609,13));NpcUpdate(window,nullptr);
    Require(npc.data.token==12 && npc.pending && queue.size()==1,"Update before shop packet must retain incoming quote for new window");
    MonthlyShop::BeforeNativePacket(0x131);
    Require(!npcWindow && !npc.active && destroys==1 && !queue.empty(),"replacement clears old window while retaining new reply");
    *reinterpret_cast<void**>(0x30BE7910)=window;NpcCreate(window,nullptr,nullptr);
    Require(npc.active && npc.data.month==202609 && npc.data.token==13 && !npc.pending && !npc.retryNeeded,"response completes switch and refreshes quote token");
    TabMouse(tab+4,nullptr,WM_LBUTTONDOWN,0,10,8);CheckSelect(1,-1,13);
    Receive(Quote(1,-1,14));NpcUpdate(window,nullptr);ReplaceWindow(window);
    Require(npc.data.month==-1 && !npc.pending && npc.data.months.size()==4 && ManagedBuyTab(tab),"common tab retains every month across interleaved Update and new window");
    TabDraw(tab,nullptr,5,95,nullptr);Require(tabCanvasReads==1,"common tab still draws to control canvas");
    NpcDestroy(window,nullptr);Require(!npc.active && !npcWindow,"ordinary close clears monthly state");
    NpcCreate(window,nullptr,nullptr);Require(!ManagedBuyTab(tab),"unmanaged shop after close keeps native behavior");

    Require(*reinterpret_cast<DWORD*>(0x30AF1CD4)==0x004BC713 && *reinterpret_cast<DWORD*>(0x30AF1C64)==0x004BCC5F,"exit/footer callbacks remain untouched");
    Require(*reinterpret_cast<DWORD*>(0x30AF1BAC)==0x004B63C3 && *reinterpret_cast<DWORD*>(0x30AF1BCC)==0x004B684B && *reinterpret_cast<DWORD*>(0x30AF1B5C)==0x004B65AA,"cash category creation, drawing and mouse routing remain entirely native");
    auto common=Quote(4,0);common.months={{0,1,"通用物品"}};Apply(cash,common);
    {
        // The native commodity array mixes ordinary stock, managed common stock,
        // both former monthly fashions and disabled aliases. No month filtering.
        BYTE ctx[0x36e4]{},storage[4+7*8]{},commodities[7][0x48]{},cashItems[0x244]{};
        Field(storage,0)=7;*reinterpret_cast<void**>(ctx+0x36e0)=storage+4;
        *reinterpret_cast<void**>(0x30BE7918)=ctx;
        const int ids[]={5000070,5240031,5152302,1051139,1051139,5000071,1102062};
        for(int i=0;i<7;++i){
            *reinterpret_cast<void**>(storage+4+i*8+4)=commodities[i];
            Field(commodities[i],0xc)=100+i;Field(commodities[i],0x10)=ids[i];
            Field(commodities[i],0x44)=i==5?0:1;Field(commodities[i],0x20)=4900;Field(commodities[i],0x1c)=1;
        }
        cash.data.products={{102,5152302,0,3000,1,true},{103,1051139,0,100000,1,true},{106,1102062,0,100000,1,true}};
        ApplyCash(cashItems);
        for(int i=0;i<7;++i)Require(Field(commodities[i],0x44)==(i==4 || i==5?0:1),"ordinary stock, coupons and September/October stock all appear together; disabled aliases stay hidden");
        Require(Field(commodities[3],0x20)==100000 && Field(commodities[6],0x20)==100000,"both former months retain base prices");
        cash.data.products[1].sale=false;ApplyCash(cashItems);
        Require(!Field(commodities[3],0x44) && Field(commodities[6],0x44)==1,"explicit delisting still works without hiding other fashions");
        cash.data.products[1].sale=true;ApplyCash(cashItems);
        Require(Field(commodities[3],0x44)==1,"relisting restores the item without month switching");
        bases.clear();*reinterpret_cast<void**>(0x30BE7918)=nullptr;Apply(cash,common);
    }
    *reinterpret_cast<void**>(0x30BE7914)=nullptr;SelectMonth(npc,202609);
    Require(!npc.pending && npc.retryNeeded,"missing socket never leaves pending request");
    *reinterpret_cast<void**>(0x30BE7914)=reinterpret_cast<void*>(42);
    State wrap;wrap.pending=true;wrap.sent=0xffffff00u;
    Require(!Expire(wrap,static_cast<DWORD>(wrap.sent+9999)) && Expire(wrap,static_cast<DWORD>(wrap.sent+10000)),"timeout handles tick rollover at ten seconds");

    Apply(npc,Quote(3));npcWindow=window;
    Require(Status(npc)==L"组队点数：12345678901","party balance retained without price multiplier");
    BYTE rows[4+0x48]{};*reinterpret_cast<int*>(rows)=1;*reinterpret_cast<int*>(rows+4)=1000000;
    *reinterpret_cast<void**>(window+0xc4)=rows+4;
    Buy(window,nullptr);auto& purchase=sentPackets.back();
    MonthlyShopModel::Reader r{purchase.data(),purchase.size()};
    Require(purchase.size()==15 && r.read(2)==0x3d && r.read(1)==0 && r.read(2)==0 && r.read(4)==1000000 && r.read(2)==1 && r.read(4)==0,"party purchase uses same valid native packet ABI");
    Require(Field(window,0xfc)==1,"party purchase awaits native reply");
    for(int channel:{1,2,3}){
        auto ten=Quote(channel);ten.months.pop_back();
        for(int month=7;month>=1;--month)ten.months.push_back({202600+month,20,std::to_string(month)+"月测试"});
        ten.months.push_back({-1,1,"全部"});Apply(npc,ten);
        const auto sentBeforePaging=sentPackets.size();
        const int clicksBeforePaging=clicks;
        for(int page=0;page<5;++page){
            Require(npc.page==page && npc.data.month==202610 && !npc.pending,"arrows page labels immediately without changing active quote");
            Require(MonthlyShopTabs::Hit(npc.data,page,70,8)==202610-2*page && MonthlyShopTabs::Hit(npc.data,page,150,8)==202609-2*page,"ten months map to five complete pages");
            TabMouse(tab+4,nullptr,page%2?WM_LBUTTONDBLCLK:WM_LBUTTONDOWN,0,200,8);
            TabMouse(tab+4,nullptr,WM_LBUTTONUP,0,200,8);
        }
        Require(npc.page==4 && clicks==clicksBeforePaging+4,"rapid clicks advance once each, expanded arrow works, boundary is silent");
        for(int i=0;i<6;++i)TabMouse(tab+4,nullptr,i%2?WM_LBUTTONDBLCLK:WM_LBUTTONDOWN,0,63,8);
        Require(npc.page==0 && sentPackets.size()==sentBeforePaging,"left boundary clamps; arrow browsing never sends a request");
        for(int i=0;i<4;++i)TabMouse(tab+4,nullptr,WM_LBUTTONDOWN,0,221,8);
        TabMouse(tab+4,nullptr,WM_LBUTTONDOWN,0,150,8);CheckSelect(channel,202601,12);
        Require(npc.pending && Status(npc)==(channel==3?L"组队点数：12345678901":L""),"switch preserves ordinary display without loading text");
        ten.month=202601;ten.token=13;Receive(ten);NpcUpdate(window,nullptr);ReplaceWindow(window);
        Require(npc.page==4 && npc.data.month==202601 && !npc.pending,"reply selects last month on its matching page");
        TabMouse(tab+4,nullptr,WM_LBUTTONDOWN,0,10,8);CheckSelect(channel,-1,13);
        // Paging is entirely local and remains responsive while a quote is pending.
        TabMouse(tab+4,nullptr,WM_LBUTTONDOWN,0,63,8);Require(npc.page==3 && npc.pending,"local previous page works during server round trip");
        ten.month=-1;ten.token=14;Receive(ten);NpcUpdate(window,nullptr);ReplaceWindow(window);
        Require(npc.page==3 && npc.data.month==-1 && npc.data.months.size()==11 && ManagedBuyTab(tab),"all tab preserves the browsed page and all ten labels");
        TabDraw(tab,nullptr,5,95,nullptr);
        TabMouse(tab+4,nullptr,WM_LBUTTONDBLCLK,0,200,8);Require(npc.page==4,"next arrow works after common tab switch");
        auto balance=ten;balance.points=77;Receive(balance);NpcUpdate(window,nullptr);
        Require(npc.page==4 && npc.data.points==77 && queue.empty(),"same-token point refresh preserves manually browsed page");
        TabMouse(tab+4,nullptr,WM_LBUTTONDOWN,0,150,8);CheckSelect(channel,202601,14);
        // Recreate after closing to avoid retaining this request in the next case.
        NpcDestroy(window,nullptr);npcWindow=window;
    }
    Require(clicks>0 && clicks==soundReleases && tabCanvasReads==4,"accepted clicks play/release native sound and common pages render every time");
    std::puts("PASS production monthly shop hooks: interleaved metadata/window packets, common tab labels, native control canvas, rapid clicks, wider arrows, paging bounds, native click sound, timeout/retry, cash and party purchase");
}
