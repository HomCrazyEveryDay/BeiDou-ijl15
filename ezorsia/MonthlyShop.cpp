#include "stdafx.h"
#include "MonthlyShop.h"
#include "MonthlyShopModel.h"
#include "MonthlyShopText.h"
#include "MonthlyShopTabs.h"
#include "MonthlyShopTabCanvas.h"
#include "CrashReporter.h"
#include <mutex>
#include <deque>

namespace {
using namespace MixedDyeResources;
using MonthlyShopModel::Snapshot;
struct State {
    Snapshot data;
    bool active=false,pending=false,retryNeeded=false;
    int page=0;
    DWORD sent=0;
};
State npc,cash;
bool installed=false;
void* npcWindow=nullptr;void* cashWindow=nullptr;
std::mutex queueMutex;std::deque<Snapshot> queue;
std::map<std::wstring,Object>& textCache=*new std::map<std::wstring,Object>;
std::map<std::wstring,Object>& tabCache=*new std::map<std::wstring,Object>;
std::map<std::wstring,Object>& tabAssets=*new std::map<std::wstring,Object>;
struct CommodityBase {void* object;int sale,price,count;};
std::map<int,CommodityBase> bases;
int& Field(void* p,int offset){return *reinterpret_cast<int*>(static_cast<unsigned char*>(p)+offset);}
void* Pointer(void* p,int offset){return *reinterpret_cast<void**>(static_cast<unsigned char*>(p)+offset);}
void Invalidate(void* window){if(window)reinterpret_cast<void(__thiscall*)(void*,const RECT*)>(0x009E04C9)(window,nullptr);}
void Fail(const char* stage){static std::set<std::string> reported;if(reported.insert(stage).second)CrashReporter::RecordEvent("monthly.shop", "failure=%s",stage);}
void ClickSound(){
    // CCtrlTab::OnMouseButton (4DD8BD) uses StringPool 0x4CF and PlaySE.
    // Use the native sound path so volume/mute settings continue to apply.
    DWORD name=0;
    try{
        auto pool=reinterpret_cast<void*(__cdecl*)()>(0x0079E805)();
        reinterpret_cast<DWORD*(__thiscall*)(void*,DWORD*,int)>(0x00406276)(pool,&name,0x4cf);
        if(name)reinterpret_cast<void(__cdecl*)(const char*)>(0x00989588)(reinterpret_cast<const char*>(name));
    }catch(...){Fail("click_sound");}
    if(name)reinterpret_cast<void(__thiscall*)(DWORD*)>(0x0040265E)(&name);
}
bool Send(std::vector<unsigned char> bytes) {
    void* socket=*reinterpret_cast<void**>(0x00BE7914);if(!socket)return false;
    // COutPacket+0x0c is bIsEncrypted, not a write cursor. Encode (6ECB52)
    // skips the custom encryption when it is nonzero. Native code also mutates
    // the payload in place, so give it our own writable buffer.
    struct Packet {int loopback;unsigned char* data;unsigned size;int encrypted,type;} p{0,bytes.data(),static_cast<unsigned>(bytes.size()),0,0};
    static_assert(sizeof(Packet)==20 && offsetof(Packet,encrypted)==12,"083 COutPacket ABI");
    reinterpret_cast<void(__thiscall*)(void*,Packet*)>(0x0049637B)(socket,&p);return true;
}
void SelectMonth(State& state,int month){
    state.retryNeeded=false;
    state.pending=Send(MonthlyShopModel::Select(state.data,month));
    if(state.pending)state.sent=GetTickCount();else state.retryNeeded=true;
    CrashReporter::RecordEvent("monthly.shop","select channel=%d token=%d from=%d to=%d sent=%d",state.data.channel,state.data.token,state.data.month,month,state.pending);
}
bool Expire(State& state,DWORD now){
    if(!state.pending || now-state.sent<10000)return false;
    state.pending=false;state.retryNeeded=true;
    CrashReporter::RecordEvent("monthly.shop","timeout channel=%d token=%d month=%d",state.data.channel,state.data.token,state.data.month);
    return true;
}
std::wstring Status(const State& state,bool footer=false){
    // Keep the old quote visible until the reply arrives, without flashing a
    // loading label for a normal round trip. Purchases remain guarded by pending.
    if(state.pending)return state.data.channel==3?L"组队点数："+std::to_wstring(state.data.points):L"";
    if(state.retryNeeded)return footer?L"切换失败，请重试":L"切换未完成，请重试";
    if(state.data.changed)return footer?L"价格已更新":L"价格已更新，请重新购买";
    if(state.data.channel==3)return L"组队点数："+std::to_wstring(state.data.points);
    return L"";
}
std::wstring Unicode(const std::string& text){
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(size<=0)throw E_INVALIDARG;std::wstring out(size,L' ');MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),&out[0],size);return out;
}
void Copy(void* dst,void* src,int x,int y){Check(Method<HRESULT(__stdcall*)(void*,int,int,void*,VARIANT)>(dst,0x80)(dst,x,y,src,Integer(255)));}
void Text(void* dst,const std::wstring& text,int x,int y,unsigned color=0x303030){
    auto key=std::to_wstring(color)+L":"+text;auto it=textCache.find(key);
    if(it==textCache.end()){
        if(textCache.size()>128)textCache.clear();
        auto factory=reinterpret_cast<Factory>(GetProcAddress(GetModuleHandleW(L"PCOM.dll"),"PcCreateObject"));
        it=textCache.emplace(key,MonthlyShopText::Canvas(factory,text,color)).first;
    }
    Copy(dst,it->second.p,x,y);
}
void Apply(State& state,Snapshot s){
    const bool sameQuote=state.active && state.data.channel==s.channel && state.data.token==s.token;
    state.data=std::move(s);state.active=true;state.pending=false;state.retryNeeded=false;
    CrashReporter::RecordEvent("monthly.shop","apply channel=%d token=%d month=%d",state.data.channel,state.data.token,state.data.month);
    if(state.data.month==(state.data.channel==4?0:MonthlyShopTabs::All) || sameQuote)
        state.page=(std::max)(0,(std::min)(state.page,MonthlyShopTabs::LastPage(static_cast<int>(MonthlyShopTabs::Months(state.data).size()))));
    else state.page=MonthlyShopTabs::SelectedPage(state.data);
}
bool Drain(int channel,bool opening=false){
    bool applied=false;
    std::lock_guard<std::mutex> guard(queueMutex);
    for(auto it=queue.begin();it!=queue.end();) {
        if((channel==4)==(it->channel==4)){
            // A new NPC token belongs to the following 0x131 shop window. An
            // Update between the two packets must not steal its metadata.
            if(channel!=4 && !opening && (!npc.active || npc.pending || it->channel!=npc.data.channel || it->token!=npc.data.token)){++it;continue;}
            Apply(channel==4?cash:npc,std::move(*it));it=queue.erase(it);applied=true;
        }else ++it;
    }
    return applied;
}
void* TabAsset(const std::wstring& path){
    auto found=tabAssets.find(path);if(found!=tabAssets.end())return found->second.p;
    void* manager=*reinterpret_cast<void**>(0x00BF14E8);if(!manager)throw E_POINTER;
    Value value;VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    BSTR key=SysAllocString(path.c_str());if(!key)throw E_OUTOFMEMORY;
    auto hr=Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT,VARIANT,VARIANT*)>(manager,0x1c)(manager,key,missing,missing,&value.v);
    SysFreeString(key);Check(hr);auto canvas=Query(value.v,CanvasIID());if(!canvas.p)throw E_NOINTERFACE;
    return tabAssets.emplace(path,std::move(canvas)).first->second.p;
}
void DrawTabs(void* canvas,const State& state,int left,int top){
    auto factory=reinterpret_cast<Factory>(GetProcAddress(GetModuleHandleW(L"PCOM.dll"),"PcCreateObject"));
    for(const auto& cell:MonthlyShopTabs::Cells(state.data,state.page)){
        const bool selected=cell.id==state.data.month;
        std::wstring label=cell.id==0?L"通用物品":cell.id==MonthlyShopTabs::All?L"全部":cell.id==MonthlyShopTabs::Previous?L"<":L">";
        if(cell.id>0)for(const auto& month:state.data.months)if(month.id==cell.id){label=Unicode(month.label);break;}
        auto key=std::to_wstring(cell.width)+L":"+std::to_wstring(selected)+L":"+label;
        auto it=tabCache.find(key);
        if(it==tabCache.end()){
            if(tabCache.size()>128)tabCache.clear();
            it=tabCache.emplace(key,MonthlyShopTabCanvas::Create(factory,TabAsset,label,cell.width,selected)).first;
        }
        Copy(canvas,it->second.p,left+cell.left,top);
    }
}
void DrawNpcTabs(void* tab,int left,int top){
    // Use the same control canvas as CCtrlTab during partial redraws.
    DWORD raw=0;reinterpret_cast<DWORD*(__thiscall*)(void*,DWORD*,int)>(0x004C0690)(tab,&raw,0);
    Object owned(reinterpret_cast<void*>(raw));if(raw)DrawTabs(owned.p,npc,left,top);
}
bool ManagedBuyTab(void* tab){
    return npc.active && npcWindow && *reinterpret_cast<void**>(0x00BE7910)==npcWindow && Pointer(npcWindow,0x9c)==tab;
}
void SelectNpcTab(unsigned message,int x,int y){
    // CCtrlTab selects on mouse down. Its native HitTest already routes these
    // control-local coordinates, unlike a label painted in the title/drag area.
    // Windows replaces the second down with DBLCLK during rapid repeated clicks.
    if(message!=WM_LBUTTONDOWN && message!=WM_LBUTTONDBLCLK)return;
    const int hit=MonthlyShopTabs::Hit(npc.data,npc.page,x,y);
    const int oldPage=npc.page;
    if(hit==MonthlyShopTabs::Previous)npc.page=(std::max)(0,npc.page-1);
    else if(hit==MonthlyShopTabs::Next)npc.page=(std::min)(MonthlyShopTabs::LastPage(static_cast<int>(MonthlyShopTabs::Months(npc.data).size())),npc.page+1);
    else if(MonthlyShopTabs::Selectable(npc.data,hit,npc.pending)){
        SelectMonth(npc,hit);
        if(npc.pending)ClickSound();
    }else return;
    if(npc.page!=oldPage)ClickSound();
    Invalidate(npcWindow);
}
using DrawFn=void(__thiscall*)(void*,const RECT*);
using MouseFn=void(__thiscall*)(void*,unsigned,unsigned,int,int);
using VoidFn=void(__thiscall*)(void*);
using CreateFn=void(__thiscall*)(void*,void*);
using SetRowsFn=void(__thiscall*)(void*,int);
using TabDrawFn=void(__thiscall*)(void*,int,int,const RECT*);
DrawFn npcDraw=reinterpret_cast<DrawFn>(0x00754DA0);
MouseFn tabMouse=reinterpret_cast<MouseFn>(0x004DD822);
TabDrawFn tabDraw=reinterpret_cast<TabDrawFn>(0x004DD903);
CreateFn npcCreate=reinterpret_cast<CreateFn>(0x007532CE),cashCreate=reinterpret_cast<CreateFn>(0x004B6CBE);
VoidFn cashUpdate=reinterpret_cast<VoidFn>(0x004B7A91),cashDestroy=reinterpret_cast<VoidFn>(0x004B70A8);
VoidFn npcUpdate=reinterpret_cast<VoidFn>(0x00753FCB),npcDestroy=reinterpret_cast<VoidFn>(0x00424408);
VoidFn nativeBuy=reinterpret_cast<VoidFn>(0x007561C1);
SetRowsFn nativeSetRows=reinterpret_cast<SetRowsFn>(0x004BA623);
void __fastcall NpcCreate(void* self,void*,void* arg){npcWindow=self;Drain(1,true);npcCreate(self,arg);if(npc.active)Invalidate(self);}
void __fastcall NpcUpdate(void* self,void*){
    npcUpdate(self);
    if(self==npcWindow)try{
        const bool applied=Drain(1),expired=Expire(npc,GetTickCount());
        if(applied || expired)Invalidate(self);
    }catch(...){Fail("npc_update");}
}
void __fastcall NpcDestroy(void* self,void*){
    if(self==npcWindow){npcWindow=nullptr;npc=State{};}
    npcDestroy(self);
}
void __fastcall NpcDraw(void* self,void*,const RECT* rect){
    npcDraw(self,rect);
    if(npc.active && self==npcWindow)try{
        DWORD raw=0;reinterpret_cast<DWORD*(__thiscall*)(void*,DWORD*)>(0x00425C4C)(self,&raw);Object canvas(reinterpret_cast<void*>(raw));
        if(raw){
            // Blank strip below the left header, above the tab row.
            const auto status=Status(npc);
            if(!status.empty())Text(canvas.p,status,7,78,npc.data.changed || npc.retryNeeded?0xa83220:0x303030);
        }
    }catch(...){Fail("npc_draw");}
}
void __fastcall TabDraw(void* self,void*,int x,int y,const RECT* rect){
    if(ManagedBuyTab(self)){try{DrawNpcTabs(self,x,y);return;}catch(...){Fail("npc_tabs");}}
    tabDraw(self,x,y,rect);
}
void __fastcall TabMouse(void* ui,void*,unsigned message,unsigned keys,int x,int y){
    if(ManagedBuyTab(static_cast<unsigned char*>(ui)-4)){try{SelectNpcTab(message,x,y);}catch(...){Fail("npc_tab_mouse");}return;}
    tabMouse(ui,message,keys,x,y);
}

void __fastcall SetRows(void* self,void*,int count){
    // Search and recommended lists bypass OnSale. Keep disabled products and
    // duplicate SN aliases out of their final ten native buy-button slots.
    if(cash.active && self==cashWindow && count>=0 && count<=10){
        void* ctx=*reinterpret_cast<void**>(0x00BE7918);
        auto rows=ctx?static_cast<unsigned char*>(Pointer(ctx,0x36e0)):nullptr;
        int total=rows?Field(rows,-4):0,kept=0;
        if(total>=0 && total<=50000){
            const int arrays[]={0x178,0x1a0,0x1c8,0x1f0,0x218};
            for(int i=0;i<count;++i){
                int index=Field(self,0x178+i*4);void* row=index>=0 && index<total?Pointer(rows,index*8+4):nullptr;
                if(!row || !Field(row,0x44))continue;
                if(kept!=i)for(int offset:arrays)Field(self,offset+kept*4)=Field(self,offset+i*4);
                ++kept;
            }
            for(int i=kept;i<10;++i)for(int offset:arrays)Field(self,offset+i*4)=offset<0x1f0?-1:offset==0x1f0?2:0;
            count=kept;
        }
    }
    nativeSetRows(self,count);
}

// CUICashShopItem reads ZArray<ZRef<CS_COMMODITY>> from CWvsContext+0x36e0.
// Only POD price/count/sale fields are changed, never ownership or array allocation.
void ApplyCash(void* window){
    if(!cash.active)return;
    void* ctx=*reinterpret_cast<void**>(0x00BE7918);if(!ctx)return;
    auto rows=static_cast<unsigned char*>(Pointer(ctx,0x36e0));if(!rows)return;
    int n=Field(rows,-4);if(n<0 || n>50000)throw E_INVALIDARG;
    std::map<int,const MonthlyShopModel::Product*> byId;
    for(const auto& p:cash.data.products)byId[p.item]=&p;
    for(int i=0;i<n;++i){void* row=Pointer(rows,i*8+4);if(!row)continue;
        int sn=Field(row,0xc);
        auto old=bases.find(sn);if(old==bases.end() || old->second.object!=row)bases[sn]={row,Field(row,0x44),Field(row,0x20),Field(row,0x1c)};
        auto base=bases.find(sn)->second;
        int id=reinterpret_cast<int(__thiscall*)(void*)>(0x0042873D)(static_cast<unsigned char*>(row)+0x10);
        auto found=byId.find(id);
        bool sale=base.sale!=0;
        if(found!=byId.end()){
            const auto& p=*found->second;sale=p.sn==sn && p.sale;
            Field(row,0x20)=p.price;Field(row,0x1c)=p.count;
        }
        Field(row,0x44)=sale?1:0;
    }
    // Category hidden counts drive the native page selector. Update them with the same filtered rows.
    void* owner=Pointer(window,0x240);if(!owner)return;
    auto categories=static_cast<unsigned char*>(Pointer(owner,0x44));
    if(categories){int count=Field(categories,-4);if(count<0 || count>1000)throw E_INVALIDARG;
        for(int i=0;i<count;++i){auto category=categories+i*0x1c;int start=Field(category,0x10),total=Field(category,0x14);
            if(start<0 || total<0 || start>n || total>n-start)throw E_INVALIDARG;
            int hidden=0;for(int j=0;j<total;++j){auto row=Pointer(rows,(start+j)*8+4);if(!row || !Field(row,0x44))++hidden;}
            Field(category,0x18)=hidden;
        }
    }
    Field(owner,0x534)=0;
    reinterpret_cast<VoidFn>(0x004B96B5)(window);
    reinterpret_cast<VoidFn>(0x004B988A)(window);
    Invalidate(window);
}
void __fastcall CashCreate(void* self,void*,void* arg){cashWindow=self;bases.clear();Drain(4);cashCreate(self,arg);try{ApplyCash(self);}catch(...){Fail("cash_create");}}
void __fastcall CashUpdate(void* self,void*){
    try{if(Drain(4) && cash.active)ApplyCash(self);}catch(...){Fail("cash_update");}
    cashUpdate(self);
}
void __fastcall CashDestroy(void* self,void*){
    // The native commodity objects remain owned by CWvsContext. Restore before native teardown.
    for(auto& pair:bases){auto& b=pair.second;Field(b.object,0x44)=b.sale;Field(b.object,0x20)=b.price;Field(b.object,0x1c)=b.count;}
    bases.clear();cash=State{};cashWindow=nullptr;cashDestroy(self);
}
void __fastcall Buy(void* self,void*){
    if(npc.active && npc.pending)return;
    if(!npc.active || npc.data.channel!=3){nativeBuy(self);return;}
    if(npc.pending || Field(self,0xfc))return;
    int index=Field(self,0xf0);void* tab=Pointer(self,0x9c);bool second=tab && Field(tab,0x3c)!=0;
    auto items=static_cast<unsigned char*>(Pointer(self,second?0xc8:0xc4));
    if(!items || index<0 || index>=Field(items,-4))return;
    auto item=items+index*0x48;int slot=index;
    if(second){void* slots=Pointer(self,0xcc);if(!slots || index>=Field(slots,-4))return;slot=Field(slots,index*4);}
    int id=reinterpret_cast<int(__thiscall*)(void*)>(0x0042873D)(item);
    std::vector<unsigned char> bytes;MonthlyShopModel::Put(bytes,0x3d,2);MonthlyShopModel::Put(bytes,0,1);
    MonthlyShopModel::Put(bytes,slot,2);MonthlyShopModel::Put(bytes,id,4);MonthlyShopModel::Put(bytes,1,2);MonthlyShopModel::Put(bytes,0,4);
    if(Send(bytes))Field(self,0xfc)=1;
}
bool Patch(DWORD address,DWORD expected,void* replacement){if(*reinterpret_cast<DWORD*>(address)!=expected)return false;Memory::WriteInt(address,reinterpret_cast<int>(replacement));return true;}
}

bool MonthlyShop::Install(){
    const std::pair<DWORD,DWORD> guards[]={{0x00AFE11C,0x007532CE},{0x00AFE13C,0x00754DA0},{0x00AFE110,0x00753FCB},{0x00AFE120,0x00424408},{0x00AF3014,0x004DD903},{0x00AF2FB4,0x004DD822},
        {0x00AF1C30,0x004B6CBE},{0x00AF1C24,0x004B7A91},{0x00AF1C34,0x004B70A8}};
    for(const auto& p:guards)if(*reinterpret_cast<DWORD*>(p.first)!=p.second)return false;
    const unsigned char buy[]={0xb8,0xa2,0x38,0xab,0};
    const unsigned char rows[]={0xb8,0xf8,0x3f,0xa8,0};
    if(std::memcmp(reinterpret_cast<void*>(0x007561C1),buy,sizeof(buy)) ||
       std::memcmp(reinterpret_cast<void*>(0x004BA623),rows,sizeof(rows)) ||
       *reinterpret_cast<unsigned short*>(0x004B9BE6)!=0x7d74)return false;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&nativeBuy),Buy))return false;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&nativeSetRows),SetRows)){
        Memory::SetHook(false,reinterpret_cast<void**>(&nativeBuy),Buy);return false;
    }
    // Native lists assume every OnSale=0 row follows all sale rows and break early.
    // Disabled SN aliases create holes: take the existing ZRef cleanup + next-row
    // path at 4B9C41 instead of cleanup + end at 4B9C65. No ownership changes.
    Memory::WriteByte(0x004B9BE7,0x59);
    Patch(0x00AFE11C,0x007532CE,NpcCreate);Patch(0x00AFE13C,0x00754DA0,NpcDraw);
    Patch(0x00AFE110,0x00753FCB,NpcUpdate);Patch(0x00AFE120,0x00424408,NpcDestroy);
    Patch(0x00AF3014,0x004DD903,TabDraw);Patch(0x00AF2FB4,0x004DD822,TabMouse);
    Patch(0x00AF1C30,0x004B6CBE,CashCreate);Patch(0x00AF1C24,0x004B7A91,CashUpdate);Patch(0x00AF1C34,0x004B70A8,CashDestroy);
    installed=true;return true;
}
bool MonthlyShop::HandlePacket(const unsigned char* data,unsigned short length){
    if(!data || length<6 || data[4]!=0x0f || data[5]!=0x10)return false;
    Snapshot s;if(!installed || !MonthlyShopModel::Decode(data+6,length-6,s))return true;
    std::lock_guard<std::mutex> guard(queueMutex);if(queue.size()>=8)queue.pop_front();queue.push_back(std::move(s));return true;
}
void MonthlyShop::BeforeNativePacket(unsigned short opcode){
    if(!installed)return;
    if(opcode==0x131){
        bool incoming=false;{std::lock_guard<std::mutex> guard(queueMutex);for(const auto& s:queue)if(s.channel!=4)incoming=true;}
        const int previousPage=npc.page;
        if(npc.active && npcWindow && *reinterpret_cast<void**>(0x00BE7910)==npcWindow){
            // CDialog::SetRet destroys the old shop without sending the native 'leave shop' packet.
            reinterpret_cast<void(__thiscall*)(void*,int)>(0x004244B1)(npcWindow,0);
        }
        npcWindow=nullptr;if(!incoming)npc=State{};else npc.page=previousPage;
    } else if(opcode==0x7d || opcode==0x7f){npc=State{};npcWindow=nullptr;}
}
bool MonthlyShop::DrawPointCurrency(void* canvas,int x,int y){
    if(!installed || !npc.active || npc.data.channel!=3)return false;
    try{Text(canvas,L"点",x,y,0x835728);return true;}catch(...){return false;}
}
