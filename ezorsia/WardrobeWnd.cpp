#include "stdafx.h"
#include "WardrobeWnd.h"
#include "WardrobeModel.h"
#include "WardrobeKeybind.h"
#include "WardrobeInput.h"
#include "ImeFocus.h"
#include "WardrobeLayout.h"
#include "WardrobePreview.h"
#include "MixedDyeResources.h"
#include "CrashReporter.h"
#include "Memory.h"
#include <deque>
#include <mutex>
#include <imm.h>
#include <mmsystem.h>
#pragma comment(lib,"imm32.lib")
#pragma comment(lib,"winmm.lib")

namespace {
using namespace MixedDyeResources;
using namespace WardrobeModel;
using WardrobeLayout::Width;
using WardrobeLayout::Height;
enum class Dialog {None,Save,Rename,Delete,Binding,Overwrite,Details};
struct Control {int id,x,y,w,h;bool enabled;std::uint64_t uid;};
struct State {
    Model model;bool installed=false,ready=false,shown=false,field=false,opening=false,pending=false,avatarDirty=false,deferredClose=false;
    unsigned syncAttempts=0;DWORD lastSync=0;
    unsigned char* window=nullptr;DWORD mainTable[14]{},uiTable[19]{},refTable[1]{};
    unsigned char look[0x1c5]{},tooltip[1304]{};bool tooltipReady=false;
    DWORD tooltipItem[2]{};std::map<int,std::string> itemNames;
    std::map<int,unsigned> ringEffects;
    std::map<std::wstring,Object> assets,texts;std::map<int,Object> icons;
    DWORD lastHeartbeat=0,lastHeartbeatReply=0;
    std::vector<Control> controls;int hover=-1,pressed=-1,selected=-1,selectedSource=0,preset=-1;
    int page=0,source=0,category=0,gridPage[3]{},edit=-1,dropdown=-1,menuPreset=-1,applyingPreset=-1;
    std::map<short,std::uint64_t> applyDraft;
    std::uint64_t clickUid=0;
    std::wstring search,outfitName,status,dialogMessage;Dialog dialog=Dialog::None;std::vector<Scope> saveScope;
    int detailSlot=0,saveSlot=0,requests=0,pendingRequest=0,pendingAction=0,windowX=120,windowY=60,dragX=0,dragY=0;
    bool dragging=false;DWORD sentAt=0,lastStatusTick=0;void* localUser=nullptr;
};
State& state=*new State;
std::mutex queueMutex;std::deque<Snapshot> snapshots;
using KeyHandler=void(__fastcall*)(void*,void*,unsigned,unsigned);
KeyHandler rootKey=reinterpret_cast<KeyHandler>(0x009E2F05);
using MouseHandler=int(__fastcall*)(void*,void*,unsigned,unsigned,int);
MouseHandler rootMouse=reinterpret_cast<MouseHandler>(0x009E3AE6);
const std::wstring root=L"UI/Wardrobe.img/";
void Invalidate();void Activate(int id);void CloseWindow();void OpenWindow();void RebuildAvatar();

std::wstring Wide(const std::string& s) {
    if(s.empty())return {};int n=MultiByteToWideChar(936,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(n<=0)return L"?";
    std::wstring out(n,L'\0');MultiByteToWideChar(936,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),&out[0],n);return out;
}
std::string Narrow(const std::wstring& s) {
    if(s.empty())return {};BOOL fallback=FALSE;int n=WideCharToMultiByte(936,WC_NO_BEST_FIT_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,&fallback);
    if(n<=0 || fallback)throw E_INVALIDARG;std::string out(n,'\0');
    WideCharToMultiByte(936,WC_NO_BEST_FIT_CHARS,s.data(),int(s.size()),&out[0],n,nullptr,&fallback);if(fallback)throw E_INVALIDARG;return out;
}
Factory FactoryFn(){return reinterpret_cast<Factory>(GetProcAddress(GetModuleHandleW(L"PCOM.dll"),"PcCreateObject"));}
Value Resource(const std::wstring& path) {
    auto manager=*reinterpret_cast<void**>(0x00BF14E8);if(!manager)throw E_POINTER;
    Value value;VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    BSTR name=SysAllocString(path.c_str());if(!name)throw E_OUTOFMEMORY;
    auto hr=Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT,VARIANT,VARIANT*)>(manager,0x1c)(manager,name,missing,missing,&value.v);
    SysFreeString(name);Check(hr);return value;
}
unsigned RingEffects(int id){
    if(id/10000!=111)return 0;
    auto cached=state.ringEffects.find(id);if(cached!=state.ringEffects.end())return cached->second;
    try {
        auto value=Resource(L"Character/Ring/0"+std::to_wstring(id)+L".img/info");
        auto info=Query(value.v,PropertyIID());if(!info.p)throw E_NOINTERFACE;
        unsigned effects=0;
        for(unsigned bit=0;bit<2;bit++){
            Value effect;Get(info.p,bit?L"chatBalloon":L"nameTag",&effect.v);
            if(effect.v.vt==VT_I4 && effect.v.lVal!=0)effects|=1u<<bit;
        }
        state.ringEffects[id]=effects;return effects;
    }catch(...){
        // Do not permanently cache an I/O/COM failure as an ordinary ring.
        static unsigned errors=0;if(errors++<8)CrashReporter::RecordEvent("wardrobe.ring","metadata_failed id=%d",id);
        return 0;
    }
}
std::string ItemName(int id){
    auto cached=state.itemNames.find(id);if(cached!=state.itemNames.end())return cached->second;
    const wchar_t* group=L"Accessory";
    switch(id/10000){
        case 100:group=L"Cap";break;case 104:group=L"Coat";break;case 105:group=L"Longcoat";break;
        case 106:group=L"Pants";break;case 107:group=L"Shoes";break;case 108:group=L"Glove";break;
        case 109:group=L"Shield";break;case 110:group=L"Cape";break;case 111:group=L"Ring";break;
        case 180:group=L"PetEquip";break;case 190:case 191:case 193:group=L"Taming";break;
        case 194:case 195:case 196:case 197:group=L"Dragon";break;
        default:if(id/10000>=130 && id/10000<=170)group=L"Weapon";break;
    }
    std::string name=std::to_string(id);
    try {auto value=Resource(std::wstring(L"String/Eqp.img/Eqp/")+group+L"/"+std::to_wstring(id)+L"/name");
        if(value.v.vt==VT_BSTR && value.v.bstrVal)name=Narrow(value.v.bstrVal);
    }catch(...){/* Native WZ may lack a translation; retain the visible item ID. */}
    if(state.itemNames.size()>=2048)state.itemNames.clear();
    state.itemNames.emplace(id,name);return name;
}
void* Canvas(const std::wstring& path) {
    auto found=state.assets.find(path);if(found!=state.assets.end())return found->second.p;
    auto value=Resource(path);auto canvas=Query(value.v,CanvasIID());if(!canvas.p)throw E_NOINTERFACE;
    return state.assets.emplace(path,std::move(canvas)).first->second.p;
}
void Copy(void* dst,void* src,int x,int y){if(src)Check(Method<HRESULT(__stdcall*)(void*,int,int,void*,VARIANT)>(dst,0x80)(dst,x,y,src,Integer(255)));}
void Image(void* dst,const wchar_t* key,int x,int y){Copy(dst,Canvas(root+key),x,y);}
void Rect(void* dst,int x,int y,int w,int h,unsigned color) {
    Check(Method<HRESULT(__stdcall*)(void*,int,int,int,int,unsigned)>(dst,0x8c)(dst,x,y,w,h,color|0xff000000));
}
Object TextCanvas(const std::wstring& text,unsigned color) {
    constexpr int w=760,h=18;HDC dc=CreateCompatibleDC(nullptr);if(!dc)throw E_OUTOFMEMORY;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    HFONT font=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,GB2312_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"SimSun");
    if(!bitmap || !font){if(bitmap)DeleteObject(bitmap);if(font)DeleteObject(font);DeleteDC(dc);throw E_OUTOFMEMORY;}
    auto oldBitmap=SelectObject(dc,bitmap),oldFont=SelectObject(dc,font);
    struct Cleanup{HDC dc;HBITMAP bitmap;HFONT font;HGDIOBJ oldBitmap,oldFont;~Cleanup(){SelectObject(dc,oldBitmap);SelectObject(dc,oldFont);DeleteObject(bitmap);DeleteObject(font);DeleteDC(dc);}} cleanup{dc,bitmap,font,oldBitmap,oldFont};
    std::memset(bits,0,w*h*4);SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
    RECT rect{0,0,w,h};DrawTextW(dc,text.c_str(),-1,&rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);GdiFlush();
    auto out=New(FactoryFn(),L"Canvas",CanvasIID());
    Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(out.p,0x2c)(out.p,w,h,Integer(0),Integer(2)));
    Check(Method<HRESULT(__stdcall*)(void*,LONG)>(out.p,0x54)(out.p,2));
    int tw=Int(out.p,0x38),th=Int(out.p,0x3c);if(tw<=0 || th<=0)throw E_INVALIDARG;
    for(int ty=0;ty<h;ty+=th)for(int tx=0;tx<w;tx+=tw) {
        Object raw;Check(Method<HRESULT(__stdcall*)(void*,int,int,void**)>(out.p,0x34)(out.p,tx,ty,&raw.p));
        LONG pitch=0;Value address;Check(Method<HRESULT(__stdcall*)(void*,LONG*,VARIANT*)>(raw.p,0x1c)(raw.p,&pitch,&address.v));
        RECT area{0,0,(std::min)(tw,w-tx),(std::min)(th,h-ty)};
        struct Unlock{void* p;RECT area;~Unlock(){Method<HRESULT(__stdcall*)(void*,RECT*)>(p,0x20)(p,&area);}} unlock{raw.p,area};
        if(address.v.vt!=(VT_BYREF|VT_UI4) || !address.v.byref || pitch<area.right*4)throw E_INVALIDARG;
        for(int y=0;y<area.bottom;y++)for(int x=0;x<area.right;x++) {
            unsigned source=static_cast<unsigned*>(bits)[(ty+y)*w+tx+x],alpha=(std::max)({source&255,(source>>8)&255,(source>>16)&255});
            unsigned pixel=(alpha<<24)|(color&0xffffff);std::memcpy(static_cast<unsigned char*>(address.v.byref)+y*pitch+x*4,&pixel,4);
        }
    }
    return out;
}
int TextWidth(const std::wstring& s){int n=0;for(wchar_t c:s)n+=c<128?6:12;return n;}
std::wstring Fit(std::wstring s,int width){while(!s.empty() && TextWidth(s)>width-12)s.pop_back();return s;}
void Text(void* dst,const std::wstring& s,int x,int y,unsigned color=0x394e5d,int width=536) {
    auto text=TextWidth(s)>width?Fit(s,width)+L"…":s;auto key=std::to_wstring(color)+L":"+text;auto found=state.texts.find(key);
    if(found==state.texts.end()){if(state.texts.size()>192)state.texts.clear();found=state.texts.emplace(key,TextCanvas(text,color)).first;}
    Copy(dst,found->second.p,x,y);
}
void Line(void* dst,int x,int y,int w){Rect(dst,x,y,w,1,0xb5c4cf);Rect(dst,x,y+1,w,1,0xffffff);}
void Add(int id,int x,int y,int w,int h,bool enabled=true,std::uint64_t uid=0){state.controls.push_back({id,x,y,w,h,enabled,uid});}
void Button(void* dst,int id,int x,int y,int w,const std::wstring& text,bool enabled=true) {
    const wchar_t* variant=!enabled?L"disabled":state.pressed==id?L"pressed":state.hover==id?L"mouseOver":L"normal";
    auto path=root+L"button"+std::to_wstring(w)+L"/"+variant+L"/0";Copy(dst,Canvas(path),x,y);
    Text(dst,text,x+(w-TextWidth(text))/2,y+6,enabled?0xffffff:0xbfc4c8,w);Add(id,x,y,w,24,enabled);
}
void Field(void* dst,int id,int x,int y,int w,const std::wstring& text,bool editable=false) {
    Rect(dst,x,y,w,24,0x8d9da6);Rect(dst,x+1,y+1,w-2,22,0xffffff);Rect(dst,x+1,y+1,w-2,1,0xd3d9dd);
    int reserve=id==8&&state.page==0?24:0;
    Text(dst,text,x+8,y+6,editable?0x303a40:0x83929d,w-18-reserve);Add(id,x,y,w,24);
    if(state.edit==id)Rect(dst,x+8+(std::min)(TextWidth(text),w-20-reserve),y+5,1,14,0x33556b);
}
void Icon(void* dst,int id,int x,int y) {
    auto found=state.icons.find(id);
    if(found==state.icons.end()) {
        auto info=*reinterpret_cast<void**>(0x00BE78D8);if(!info)return;DWORD value=0;
        reinterpret_cast<DWORD*(__thiscall*)(void*,DWORD*,int,int,int)>(0x005D3BD8)(info,&value,id,0,0);
        if(!value)return;if(state.icons.size()>600)state.icons.clear();found=state.icons.emplace(id,Object(reinterpret_cast<void*>(value))).first;
    }
    Copy(dst,found->second.p,x+(32-Int(found->second.p,0x40))/2,y+(32-Int(found->second.p,0x48))/2);
}
const wchar_t* SlotName(short slot) {
    switch(slot){case -101:return L"帽子";case -102:return L"脸饰";case -103:return L"眼饰";case -104:return L"耳饰";
    case -105:return L"衣服";case -106:return L"裤裙";case -107:return L"鞋子";case -108:return L"手套";
    case -109:return L"披风";case -110:return L"盾牌";case -111:return L"武器";case -112:return L"戒指1";
    case -113:return L"戒指2";case -115:return L"戒指3";case -116:return L"戒指4";default:return L"其他";}
}
const wchar_t* Reason(unsigned reason){static const wchar_t* values[]={L"可穿",L"仅查看",L"限时时装仅查看",L"关系物品，保持原状",L"唯一道具，保持原状",L"不可共享",L"不满足穿戴条件",L"物品已锁定"};return reason<8?values[reason]:L"仅查看";}
std::wstring Lower(std::wstring s){std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return wchar_t(towlower(c));});return s;}
std::vector<int> Filter(int source,bool deposit=false) {
    std::vector<int> result;auto query=Lower(state.search);const auto& items=state.model.current.items[source];
    for(std::size_t i=0;i<items.size();i++) {
        const auto& item=items[i];if(source==2 && item.position>=-100)continue;if(deposit && !(item.flags&2))continue;
        if(state.category && Category(item.id)!=state.category)continue;
        if(!query.empty() && Lower(Wide(item.name)).find(query)==std::wstring::npos)continue;result.push_back(int(i));
    }
    return result;
}
void Grid(void* dst,int source,int x,int y,int columns,bool deposit=false) {
    auto indices=Filter(source,deposit);int count=columns*WardrobeLayout::GridRows,pages=(std::max)(1,(int(indices.size())+count-1)/count);
    auto& page=state.gridPage[source];page=(std::min)(page,pages-1);
    for(int cell=0;cell<count;cell++) {
        int gx=x+cell%columns*36,gy=y+cell/columns*36;Image(dst,L"cell",gx,gy);
        int index=page*count+cell;if(index>=int(indices.size()))continue;index=indices[index];
        const auto& item=state.model.current.items[source][index];Icon(dst,item.id,gx,gy);
        if(state.selected==index && state.selectedSource==source)Image(dst,L"select",gx-2,gy-2);
        if(state.model.IsPreviewed(item.uid)) {
            Rect(dst,gx+24,gy+24,8,8,0xdbeedc);Text(dst,L"v",gx+25,gy+23,0x376f4e,8);
        } else if(!(item.flags&1) && !(deposit && item.flags&2))Text(dst,L"-",gx+25,gy+23,0x976e6e,8);
        Add(1000+source*1024+index,gx,gy,32,32,true,item.uid);
    }
    Text(dst,(state.search.empty()?L"共 ":L"找到 ")+std::to_wstring(indices.size())+L" 件",x,WardrobeLayout::PageY+4,0x788b96,110);
    int right=x+columns*36-4;
    Text(dst,L"<",right-80,WardrobeLayout::PageY+4,page?0x335571:0xa0aeb8);Add(30+source*2,right-86,WardrobeLayout::PageY,20,18,page>0);
    Text(dst,std::to_wstring(page+1)+L" / "+std::to_wstring(pages),right-54,WardrobeLayout::PageY+4,0x536c7e,44);
    Text(dst,L">",right-6,WardrobeLayout::PageY+4,0x335571);Add(31+source*2,right-16,WardrobeLayout::PageY,20,18,page+1<pages);
}
const Item* Selected(){const auto& list=state.model.current.items[state.selectedSource];return state.selected>=0 && state.selected<int(list.size())?&list[state.selected]:nullptr;}
int CurrentPreset(){return state.model.CurrentPreset(state.preset);}
bool Writable(){return !state.deferredClose && !state.pending && !state.model.current.busy;}
void DialogButton(void* dst,int id,int x,int y,bool ok,bool enabled=true){
    const wchar_t* variant=!enabled?L"disabled":state.pressed==id?L"pressed":state.hover==id?L"mouseOver":L"normal";
    auto key=std::wstring(L"UI/Basic.img/")+(ok?L"BtOK/":L"BtCancel/")+variant+L"/0";
    auto canvas=Canvas(key);Copy(dst,canvas,x,y);Add(id,x,y,Int(canvas,0x40),Int(canvas,0x48),enabled);
}
void DrawDialog(void* dst) {
    const auto r=WardrobeLayout::Modal;const int x=r.x,y=r.y;Image(dst,L"modal",x,y);state.controls.clear();
    if(state.dialog==Dialog::Details){
        const auto& outfit=state.model.current.outfits[state.detailSlot];
        Text(dst,Wide(outfit.name),x+18,y+20,0xffffff,230);
        const wchar_t* slots[]={L"帽子",L"脸饰",L"眼饰",L"耳饰",L"衣服",L"裤子",L"鞋子",L"手套",L"披风",L"盾牌",L"武器",L"戒指",L"戒指",L"戒指",L"戒指"};
        for(int i=0;i<int(Slots.size());i++){
            const int xx=x+18+(i%5)*46,yy=y+40+(i/5)*35;
            auto entry=std::find_if(outfit.entries.begin(),outfit.entries.end(),[&](const Scope& e){return e.slot==Slots[i];});
            bool present=entry!=outfit.entries.end()&&entry->mode==2;
            Rect(dst,xx,yy,32,32,present?0xeef0df:0x315471);
            if(present){Icon(dst,entry->itemId,xx,yy);if(!state.model.DetailItem(*entry)){Rect(dst,xx,yy+30,32,2,0xd9887c);}}
            else Text(dst,slots[i],xx+4,yy+10,0x9eafbb,28);
            Add(5000+i,xx,yy,32,32);
            if(state.hover==5000+i){
                std::wstring caption=slots[i];
                if(present)caption+=L"："+Wide(ItemName(entry->itemId));
                else caption+=entry!=outfit.entries.end()&&entry->mode==0?L"：不参与搭配":L"：未穿点装";
                Text(dst,caption,x+18,y+146,0xffffff,230);
            }
        }
        DialogButton(dst,400,x+103,y+r.h-28,true);return;
    }
    const bool editing=state.dialog==Dialog::Save || state.dialog==Dialog::Rename;
    Text(dst,editing?(state.dialog==Dialog::Save?L"保存搭配":L"重命名"):L"确认",x+18,y+22,0xffffff,230);
    if(editing) {
        Field(dst,450,x+18,y+46,230,state.outfitName,true);
        if(state.dialog==Dialog::Save)for(int i=0;i<6;i++) {
            const int xx=x+18+i*39;
            Rect(dst,xx,y+82,35,24,state.saveSlot==i?0xe7f2ff:state.model.current.outfits[i].defined?0x365b7d:0x244e78);
            Text(dst,std::to_wstring(i+1),xx+14,y+88,state.saveSlot==i?0x183d63:0xffffff,20);
            Add(460+i,xx,y+82,35,24);
        }
        if(!state.dialogMessage.empty())Text(dst,state.dialogMessage,x+18,y+116,0xffdbbf,230);
    } else {
        const wchar_t* message=state.dialog==Dialog::Delete?L"删除这套搭配？":state.dialog==Dialog::Overwrite?L"覆盖这个位置的搭配？":L"此搭配含穿上后绑定的时装。";
        Text(dst,message,x+18,y+56,0xffffff,230);
        if(state.dialog==Dialog::Binding){
            std::wstring names;for(const auto& e:state.applyDraft){auto item=state.model.Find(e.second);if(item && item->flags&8){if(!names.empty())names+=L"、";names+=Wide(item->name);}}
            Text(dst,names,x+18,y+77,0xffe4b7,230);
            Text(dst,L"绑定后不能交易或存回账号衣柜。",x+18,y+102,0xffffff,230);
            Text(dst,L"确认穿上？",x+18,y+122,0xffffff,230);
        }
    }
    DialogButton(dst,editing?401:402,x+52,y+r.h-34,true,!state.pending&&(!editing||!state.outfitName.empty()));
    DialogButton(dst,400,x+149,y+r.h-34,false);
}
void DrawBody(void* window,const RECT* rect) {
    using namespace WardrobeLayout;
    reinterpret_cast<void(__thiscall*)(void*,const RECT*)>(0x009E0502)(window,rect);
    DWORD canvas=0;reinterpret_cast<DWORD*(__thiscall*)(void*,DWORD*)>(0x00425C4C)(window,&canvas);
    Object owned(reinterpret_cast<void*>(canvas));if(!canvas)return;auto dst=owned.p;
    state.controls.clear();Text(dst,L"北斗衣柜",12,5,0x111111);
    Copy(dst,Canvas(L"UI/Basic.img/BtClose/normal/0"),Width-20,5);Add(1,Width-24,2,20,20);
    for(int page=0;page<3;page++) {
        int x=16+page*78;Image(dst,state.page==page?L"tabOn":L"tabOff",x,30);
        const wchar_t* label=page==0?L"衣柜":page==1?L"搭配":L"存取";
        Text(dst,label,x+25,37,state.page==page?0xffffff:0x566774);Add(page==0?3:page==1?4:19,x,30,74,26,!state.pending);
    }
    Text(dst,L"账号衣柜  "+std::to_wstring(state.model.current.items[0].size())+L" / "+std::to_wstring(state.model.current.capacity),575,38,0x687b88,187);
    Line(dst,14,56,752);
    if(state.page!=2) {
        for(int i=0;i<int(Scenes.size());i++) {
            auto r=Scene(i);bool selected=WardrobePreview::Scene()==i;
            Image(dst,selected?L"tabOn":L"tabOff",r.x,r.y);
            Text(dst,Scenes[i],r.x+25,r.y+7,selected?0xffffff:0x566774);Add(80+i,r.x,r.y,r.w,r.h,!state.pending);
        }
        Copy(dst,Canvas(root+L"scene/"+std::to_wstring(WardrobePreview::Scene())),Preview.x,Preview.y);
        // The actual CUserPreview draws its avatar, name tag, ring effects and animations.
        // This canvas contains no painted substitute for a character/rope/ladder.
        Rect(dst,18,Preview.y-2,300,1,0x8d9da6);Rect(dst,18,Preview.y+Preview.h,300,1,0x8d9da6);
        // Preview changes happen only by double-clicking an item.
    }
    if(state.page==0) {
        Field(dst,7,338,66,80,state.source==0?L"衣柜 ▾":state.source==1?L"背包 ▾":L"身上 ▾",true);
        Field(dst,8,426,66,338,state.search.empty()&&state.edit!=8?L"搜索时装名称……":state.search,state.edit==8||!state.search.empty());
        Add(9,742,66,22,24);Text(dst,L"×",747,71,0x8a959e);
        Line(dst,338,121,426);
        // Draw inactive tabs first; the raised active tab joins the panel border last.
        for(int pass=0;pass<2;pass++)for(int i=0;i<8;i++) {
            bool on=state.category==i;if(on!=(pass==1))continue;auto r=WardrobeLayout::Category(i);
            Image(dst,on?L"categoryOn":L"categoryOff",r.x,r.y+(on?0:2));
            Text(dst,Categories[i],r.x+15,r.y+8,on?0xffffff:0x657783);Add(CategoryId(i),r.x,r.y,r.w,r.h);
        }
        Grid(dst,state.source,GridX,GridY,GridColumns);Line(dst,338,436,426);
        auto item=Selected();
        if(item){Text(dst,Wide(item->name),342,444,0x273c4c,235);Text(dst,Reason(item->reason),584,444,0x7b8b94,92);}
        if(state.dropdown==7){Rect(dst,338,90,80,62,0x8d9da6);Rect(dst,339,91,78,60,0xffffff);for(int i=0;i<3;i++){Text(dst,i==0?L"衣柜":i==1?L"背包":L"身上",349,95+i*20);Add(40+i,339,91+i*20,78,20);}}
    } else if(state.page==1) {
        Text(dst,L"我的搭配",344,74,0x456276);
        for(int i=0;i<6;i++) {
            const auto& outfit=state.model.current.outfits[i];int y=94+i*55;
            Rect(dst,338,y,426,52,state.preset==i?0xdcecf8:state.pending&&state.applyingPreset==i?0xe5edf4:0xf0f2f3);
            if(state.preset==i){Rect(dst,337,y-1,428,2,0x2f78a8);Rect(dst,337,y+51,428,2,0x2f78a8);Rect(dst,337,y,2,52,0x2f78a8);Rect(dst,763,y,2,52,0x2f78a8);}
            Text(dst,std::to_wstring(i+1),348,y+20,0x6b8291);
            Text(dst,outfit.defined?Wide(outfit.name):L"空搭配",373,y+12,0x4a6375,135);
            int pieces=int(std::count_if(outfit.entries.begin(),outfit.entries.end(),[](const Scope& e){return e.mode==2;}));
            if(outfit.defined)Text(dst,std::to_wstring(pieces)+L" 件",534,y+20,0x637f90,60);
            const auto resolved=state.model.Resolve(outfit);
            if(resolved.missing)Text(dst,L"缺 "+std::to_wstring(resolved.missing)+L" 件",373,y+31,0x966c62,135);
            else if(resolved.ambiguous)Text(dst,L"同款多件",373,y+31,0x966c62,135);
            else if(resolved.restricted)Text(dst,L"受限，无法切换",373,y+31,0x966c62,135);
            if(state.preset==i)Text(dst,L"当前",683,y+20,0x236b9c,42);
            Add(100+i,338,y,426,52,Writable());
            if(outfit.defined){Text(dst,L"详情",620,y+20,0x376f97,40);Add(210+i,610,y+9,55,34,Writable());}
            if(outfit.defined){Text(dst,L"…",738,y+20,0x526f84,20);Add(200+i,728,y+9,32,34,Writable());}
        }
        if(state.dropdown>=200 && state.dropdown<206) {
            int yy=94+(state.dropdown-200)*55+34;
            Rect(dst,672,yy,92,50,0x8198a8);Rect(dst,673,yy+1,90,48,0xf1f6fa);
            Text(dst,L"重命名",686,yy+6);Add(12,673,yy+1,90,24);
            Text(dst,L"删除",686,yy+30);Add(13,673,yy+25,90,24);
        }
    } else {
        Text(dst,L"角色背包",30,74,0x456276);
        Text(dst,L"账号共享衣柜",438,74,0x456276);
        Field(dst,8,554,66,210,state.search.empty()&&state.edit!=8?L"搜索时装名称……":state.search,state.edit==8||!state.search.empty());
        Grid(dst,1,30,GridY,9,true);Grid(dst,0,438,GridY,9);
    }
    std::wstring status=state.status;
    if(state.pending)status=GetTickCount()-state.sentAt>=10000?L"正在核对结果…":L"";
    else if(state.page==0 && !state.model.Valid())status=L"试穿中有缺失或受限的时装";
    else if(status.empty() && state.page==1 && std::any_of(Slots.begin(),Slots.end(),[](short slot){return state.model.Locked(slot);}))status=L"受保护的装备不参与搭配切换。";
    Text(dst,status,16,StatusY,0x52728a,748);Line(dst,6,FooterY-7,768);
    if(state.page==0)Button(dst,18,128,FooterY,80,L"保存搭配",Writable()&&state.model.Valid());
    if(state.page==2){
        auto selected=Selected();bool transferable=selected && selected->uid && (selected->flags&2);
        Button(dst,20,354,232,80,L"存入 →",Writable()&&state.selectedSource==1&&transferable);
        Button(dst,21,354,272,80,L"← 取出",Writable()&&state.selectedSource==0&&transferable);
        Button(dst,22,354,312,80,L"全部存入 →",Writable()&&state.model.StorableCount()>0);
    }
    if(state.dialog!=Dialog::None)DrawDialog(dst);
}

// Native lifecycle and interaction. Graphics and inventory previews stay on the game thread.
void Invalidate(){if(state.ready)reinterpret_cast<void(__thiscall*)(void*,const RECT*)>(0x009E04C9)(state.window,nullptr);}
void Focus(bool on){
    auto manager=*reinterpret_cast<unsigned char**>(0x00BEC20C);if(!manager || !state.window)return;
    if(on || *reinterpret_cast<void**>(manager+0x88)==state.window+4)
        reinterpret_cast<void(__thiscall*)(void*,void*)>(0x009E3264)(manager,on?state.window+4:manager+4);
}
void Edit(int id){
    int previous=state.edit;state.edit=id;
    auto manager=*reinterpret_cast<unsigned char**>(0x00BEC20C);
    if(Client::imeType!=0) {
        // SetFocus installs the new target before notifying us of focus loss.
        if(id>=0) {
            if(manager && state.window && *reinterpret_cast<void**>(manager+0x88)==state.window+4)
                EnableImeForControl(state.window+4);
        } else if(previous>=0 && state.window) DisableImeForControl(state.window+4);
    } else if(manager && state.window && *reinterpret_cast<void**>(manager+0x88)==state.window+4 && (id>=0 || previous>=0)) {
        // EnableIME is CWndMan::__thiscall, not a process-global stdcall.
        reinterpret_cast<void(__thiscall*)(void*,int)>(0x009E85F3)(manager,id>=0?1:0);
    }
}
void TooltipClear(){
    if(state.tooltipReady)reinterpret_cast<void(__thiscall*)(void*)>(0x008E6E23)(state.tooltip);
    if(state.tooltipItem[1])reinterpret_cast<void(__thiscall*)(DWORD*)>(0x004288C1)(state.tooltipItem);
    state.tooltipItem[0]=state.tooltipItem[1]=0;
}
void Cursor(int shape=-1){
    auto input=*reinterpret_cast<void**>(0x00BEC33C);if(!input)return;
    // ProcessKey hides the native cursor; our private mouse route must restore it.
    if(!reinterpret_cast<int(__thiscall*)(void*)>(0x009E3FD5)(input))
        reinterpret_cast<void(__thiscall*)(void*,int)>(0x0059A338)(input,1);
    if(shape>=0)reinterpret_cast<void(__thiscall*)(void*,int)>(0x0059A6D9)(input,shape);
}
void ReleaseAvatar(){WardrobePreview::Dispose();}
void Show(bool visible){
    state.shown=visible;if(!state.ready)return;
    if(!visible){state.deferredClose=true;return;}
    for(int offset:{0x18,0x1c,0x20}){auto layer=*reinterpret_cast<void**>(state.window+offset);if(layer)Check(Method<HRESULT(__stdcall*)(void*,int)>(layer,0x11c)(layer,visible?1:0));}
    WardrobePreview::Update(visible&&state.page!=2&&state.dialog==Dialog::None,false);
    if(visible)state.avatarDirty=true;Invalidate();
}
bool Send(unsigned action,const std::vector<unsigned char>& payload={},bool query=false,int tokenOverride=0){
    if(state.deferredClose && action!=Close)return false;
    auto socket=*reinterpret_cast<void**>(0x00BE7914);if(!socket)return false;
    bool control=action==Close || action==Heartbeat;
    if(!query && !control && state.pending)return false;
    int request=control?0:query?state.pendingRequest:++state.requests;
    std::vector<unsigned char> bytes;Number(bytes,0x1001,2);Number(bytes,Magic,2);Number(bytes,Version,1);Number(bytes,action,1);
    Number(bytes,action==Open?0:tokenOverride?tokenOverride:state.model.current.token,4);Number(bytes,action==Open?0:state.model.current.revision,4);Number(bytes,request,4);
    bytes.insert(bytes.end(),payload.begin(),payload.end());
    struct Packet {int loopback;unsigned char* data;unsigned long size;unsigned offset;int shanda;} packet{0,bytes.data(),static_cast<unsigned long>(bytes.size()),0,0};
    reinterpret_cast<void(__thiscall*)(void*,Packet*)>(0x0049637B)(socket,&packet);
    if(!query && !control){state.pending=true;state.pendingRequest=request;state.pendingAction=action;state.syncAttempts=0;state.lastSync=state.sentAt=GetTickCount();}
    return true;
}
void HideWindowLayers(){
    if(!state.ready || !state.window)return;
    // Native CWnd::Destroy releases these references without hiding them. A
    // quarantined preview may still retain its parent after a failed destructor.
    for(int offset:{0x18,0x1c,0x20})if(auto layer=*reinterpret_cast<void**>(state.window+offset))
        Method<HRESULT(__stdcall*)(void*,int)>(layer,0x11c)(layer,0);
}
void CloseWindow(){
    if(WardrobePreview::Busy()){state.deferredClose=true;return;}
    if(state.model.current.token)try{Send(Close);}catch(...){CrashReporter::RecordEvent("wardrobe.window","close_send_failed");}
    HideWindowLayers();
    WardrobeInput::Unlock();Edit(-1);Focus(false);TooltipClear();ReleaseAvatar();state.shown=false;state.opening=false;state.dragging=false;
    Cursor(0);
    auto manager=*reinterpret_cast<unsigned char**>(0x00BEC20C);
    if(manager && state.window && *reinterpret_cast<void**>(manager+0x90)==state.window+4)reinterpret_cast<void(__thiscall*)(void*)>(0x009E3AC0)(manager);
    if(state.ready){state.ready=false;reinterpret_cast<void(__thiscall*)(void*)>(0x009E00AF)(state.window);}
    state.pending=false;state.model=Model{};state.dialog=Dialog::None;state.dropdown=-1;state.hover=state.pressed=state.selected=-1;
    state.avatarDirty=false;state.lastHeartbeat=state.lastHeartbeatReply=0;state.texts.clear();state.controls.clear();
}
void Fail(const char* stage){
    if(!state.deferredClose)CrashReporter::RecordEvent("wardrobe.window","failed stage=%s token=%d request=%d",stage,state.model.current.token,state.pendingRequest);
    // A CWnd must not destroy its registration/layers from inside its own Draw callback.
    state.deferredClose=true;state.controls.clear();
}
bool FinishDeferredClose(){
    if(!state.deferredClose)return false;
    if(!WardrobePreview::Busy())try{CloseWindow();state.deferredClose=false;}catch(...){Fail("deferred_close");}
    return true;
}
void Popup(Dialog dialog){Edit(-1);TooltipClear();state.hover=state.pressed=-1;state.controls.clear();state.dialog=dialog;state.dialogMessage.clear();state.dropdown=-1;state.avatarDirty=true;Invalidate();}
void ResetSelection(){state.selected=state.hover=state.pressed=-1;state.clickUid=0;TooltipClear();}
void PreviewSelected(){
    if(!Writable())return;auto item=Selected();if(!item)return;
    if(!state.model.Toggle(item->uid)){state.status=L"这件时装暂时不能试穿。";Invalidate();return;}
    state.status.clear();state.avatarDirty=true;Invalidate();
}
void TransferSelected(){
    if(!Writable() || (state.selectedSource!=0 && state.selectedSource!=1))return;
    auto item=Selected();if(!item || !item->uid || !(item->flags&2))return;
    std::vector<unsigned char> bytes;Number(bytes,item->uid,8);
    if(Send(state.selectedSource==1?Store:Take,bytes)){TooltipClear();state.status.clear();}
    else state.status=L"发送失败，请重新打开衣柜。";
    Invalidate();
}
void StoreAll(){
    if(!Writable() || state.page!=2)return;
    int count=state.model.StorableCount();
    if(!count){state.status=L"背包中没有可存入的时装。";Invalidate();return;}
    if(int(state.model.current.items[0].size())+count>state.model.current.capacity){
        state.status=L"衣柜空位不足，需要 "+std::to_wstring(count)+L" 个空位，未存入。";Invalidate();return;
    }
    // Real item identities are positive. STORE uid=0 requests one atomic batch
    // of eligible equipment-bag items, with the server rechecking the scope.
    std::vector<unsigned char> bytes;Number(bytes,0,8);
    if(Send(Store,bytes)){TooltipClear();state.status.clear();}
    else state.status=L"发送失败，请重新打开衣柜。";
    Invalidate();
}
void ApplyDraft(bool binding){
    if(!Writable() || state.applyingPreset<0 || state.applyDraft.empty())return;
    std::vector<unsigned char> bytes;Number(bytes,binding?1:0,1);Number(bytes,state.applyDraft.size(),1);
    for(const auto& e:state.applyDraft){Number(bytes,e.first,2);Number(bytes,e.second,8);}
    Popup(Dialog::None);if(!Send(Apply,bytes)){state.applyingPreset=-1;state.applyDraft.clear();state.status=L"发送失败，请重新打开衣柜。";}
    Invalidate();
}
void SaveOutfit(){
    if(state.pending || state.outfitName.empty() || state.outfitName.size()>8)return;
    std::string name;try{name=Narrow(state.outfitName);}catch(...){state.dialogMessage=L"名称含不支持的字符，请修改。";Invalidate();return;}
    std::vector<unsigned char> bytes;Number(bytes,state.saveSlot+1,1);String(bytes,name);
    if(state.dialog!=Dialog::Rename){Number(bytes,state.saveScope.size(),1);for(const auto& e:state.saveScope){Number(bytes,e.slot,2);Number(bytes,e.mode,1);Number(bytes,e.mode==2?e.uid:0,8);}}
    auto action=state.dialog==Dialog::Rename?Rename:Save;
    if(Send(action,bytes))Popup(Dialog::None);else state.status=L"发送失败，请重新打开衣柜。";
}
void BeginSave(int slot=-1){
    if(!Writable() || !state.model.original && !state.model.Valid())return;state.saveScope=state.model.SaveScope();state.saveSlot=slot>=0?slot:0;
    if(slot<0)for(int i=0;i<6;i++)if(!state.model.current.outfits[i].defined){state.saveSlot=i;break;}
    const auto& existing=state.model.current.outfits[state.saveSlot];
    state.outfitName=existing.defined?Wide(existing.name):L"搭配"+std::to_wstring(state.saveSlot+1);Popup(Dialog::Save);Edit(450);
}
void LoadPreset(int index){
    if(!Writable() || index<0 || index>=6)return;
    Model candidate=state.model;
    const auto resolved=candidate.Resolve(candidate.current.outfits[index]);
    if(!candidate.Load(candidate.current.outfits[index])){
        state.status=resolved.missing?L"缺少 "+std::to_wstring(resolved.missing)+L" 件时装，未切换搭配。":resolved.ambiguous?L"有多件同款，请重新保存这套搭配；本次未切换。":L"有时装无法穿戴、存回衣柜或部位冲突，未切换搭配。";
        Invalidate();return;
    }
    state.status.clear();state.dropdown=-1;
    if(candidate.draft.empty()){
        if(!state.model.MatchesCurrent(candidate.current.outfits[index])){
            state.status=L"这套搭配没有可切换的部位。";
        }
        state.preset=state.model.CurrentPreset(index);state.avatarDirty=true;Invalidate();return;
    }
    state.applyingPreset=index;state.applyDraft=std::move(candidate.draft);
    bool binding=false;for(const auto& e:state.applyDraft){auto item=state.model.Find(e.second);if(item && item->flags&8)binding=true;}
    if(binding)Popup(Dialog::Binding);else ApplyDraft(false);
}
void Activate(int id){
    if(state.deferredClose)return;
    if(id==1){CloseWindow();return;}
    if(id==400){
        auto dialog=state.dialog;
        if(dialog==Dialog::Binding){state.applyingPreset=-1;state.applyDraft.clear();}
        Popup(dialog==Dialog::Overwrite?Dialog::Save:Dialog::None);if(dialog==Dialog::Overwrite)Edit(450);return;
    }
    if(state.pending)return;
    if(id==402){
        if(state.dialog==Dialog::Binding){ApplyDraft(true);return;}
        if(state.dialog==Dialog::Overwrite){SaveOutfit();return;}
        if(state.dialog==Dialog::Delete && state.menuPreset>=0){std::vector<unsigned char> bytes;Number(bytes,state.menuPreset+1,1);if(Send(Delete,bytes))Popup(Dialog::None);return;}
    }
    if(id==450 || id==8){state.dropdown=-1;Edit(id);Invalidate();return;}
    Edit(-1);
    if(id>=1000 && id<4072){state.selectedSource=(id-1000)/1024;state.selected=(id-1000)%1024;}
    else if(id==3 || id==4){
        state.page=id-3;state.dropdown=-1;
        state.model.original=state.page==1;if(state.page==1)state.preset=state.model.CurrentPreset();
        ResetSelection();state.avatarDirty=true;
    }
    else if(id==7)state.dropdown=state.dropdown==7?-1:7;
    else if(id==9){state.search.clear();std::fill(std::begin(state.gridPage),std::end(state.gridPage),0);ResetSelection();}
    else if(id==12 && state.menuPreset>=0){state.saveSlot=state.menuPreset;state.outfitName=Wide(state.model.current.outfits[state.menuPreset].name);Popup(Dialog::Rename);Edit(450);}
    else if(id==13 && state.menuPreset>=0)Popup(Dialog::Delete);
    else if(id==18)BeginSave();
    else if(id==19){state.page=2;state.model.original=false;state.category=0;state.dropdown=-1;ResetSelection();state.avatarDirty=true;}
    else if(id==20 && state.page==2 && state.selectedSource==1)TransferSelected();
    else if(id==21 && state.page==2 && state.selectedSource==0)TransferSelected();
    else if(id==22)StoreAll();
    else if(id>=300 && id<=307){state.category=id-300;std::fill(std::begin(state.gridPage),std::end(state.gridPage),0);ResetSelection();}
    else if(id>=30 && id<=35){state.gridPage[(id-30)/2]+=(id%2)?1:-1;ResetSelection();}
    else if(id>=40 && id<=42){state.source=id-40;state.dropdown=-1;ResetSelection();}
    else if(id>=80 && id<=82)WardrobePreview::SetScene(id-80);
    else if(id>=100 && id<106){if(state.model.current.outfits[id-100].defined)LoadPreset(id-100);else BeginSave(id-100);}
    else if(id>=200 && id<206){state.menuPreset=id-200;state.dropdown=state.dropdown==id?-1:id;}
    else if(id>=210 && id<216){state.detailSlot=id-210;Popup(Dialog::Details);}
    else if(id==401){
        try{Narrow(state.outfitName);}catch(...){state.dialogMessage=L"名称含不支持的字符，请修改。";Invalidate();return;}
        if(state.dialog==Dialog::Save && state.model.current.outfits[state.saveSlot].defined)Popup(Dialog::Overwrite);else SaveOutfit();
    }
    else if(id>=460 && id<466 && state.dialog==Dialog::Save){
        const auto& before=state.model.current.outfits[state.saveSlot];
        bool defaultName=state.outfitName==(before.defined?Wide(before.name):L"搭配"+std::to_wstring(state.saveSlot+1));
        state.saveSlot=id-460;const auto& after=state.model.current.outfits[state.saveSlot];
        if(defaultName)state.outfitName=after.defined?Wide(after.name):L"搭配"+std::to_wstring(state.saveSlot+1);
    }
    Invalidate();
}
int Hit(int x,int y){
    if(state.deferredClose)return -1;
    if(!state.ready || !state.shown)return -1;
    for(auto i=state.controls.rbegin();i!=state.controls.rend();++i)if(x>=i->x && y>=i->y && x<i->x+i->w && y<i->y+i->h){
        if(!i->enabled)return -1;
        if(i->id>=1000 && i->id<4072){
            const auto& items=state.model.current.items[(i->id-1000)/1024];int index=(i->id-1000)%1024;
            if(index>=int(items.size()) || items[index].uid!=i->uid)return -1;
        }
        return i->id;
    }
    return -1;
}
void Hover(int id,int x,int y){
    Cursor(id>=1000?5:id>=0?4:0);
    if(id==state.hover)return;int previous=state.hover;state.hover=id;TooltipClear();
    // Item hover only changes the native tooltip layer, not the catalog canvas.
    if((previous>=0 && previous<1000)||(id>=0 && id<1000)||previous>=5000||id>=5000)Invalidate();
    if(state.pending)return;
    const Item* hovered=nullptr;
    if(id>=5000 && id<5015 && state.dialog==Dialog::Details){
        const auto& outfit=state.model.current.outfits[state.detailSlot];
        auto entry=std::find_if(outfit.entries.begin(),outfit.entries.end(),[&](const Scope& e){return e.slot==Slots[id-5000];});
        if(entry!=outfit.entries.end())hovered=state.model.DetailItem(*entry);
    } else if(id>=1000 && id<4072){
        int source=(id-1000)/1024,index=(id-1000)%1024;
        if(index<int(state.model.current.items[source].size()))hovered=&state.model.current.items[source][index];
    }
    if(!hovered)return;const auto& item=*hovered;
    try {
        if(!state.tooltipReady){reinterpret_cast<void(__thiscall*)(void*)>(0x008E49B5)(state.tooltip);state.tooltipReady=true;}
        auto info=*reinterpret_cast<void**>(0x00BE78D8);if(!info)throw E_POINTER;
        reinterpret_cast<DWORD*(__thiscall*)(void*,DWORD*,int)>(0x005D5D95)(info,state.tooltipItem,item.id);
        auto native=reinterpret_cast<void*>(state.tooltipItem[1]);if(!native)throw E_POINTER;
        if(*reinterpret_cast<DWORD*>(native)!=0x00AF310C)throw E_NOINTERFACE;
        // v83 GW_ItemSlotEquip::Decode at vtable+70; consumes the normal inventory record.
        struct Packet {int loopback=0,state=2;const void* data;unsigned short length,sequence=0;unsigned unknown=0,offset=1;};
        static_assert(offsetof(Packet,offset)==0x14,"Native packet offset");
        Packet packet{0,2,item.native.data(),static_cast<unsigned short>(item.native.size())};
        Method<void(__thiscall*)(void*,Packet*)>(native,0x70)(native,&packet);
        if(packet.offset!=item.native.size())throw E_INVALIDARG;
        DWORD options[11]{};reinterpret_cast<void(__thiscall*)(void*)>(0x00483EED)(options);
        struct Options {DWORD* p;~Options(){reinterpret_cast<void(__thiscall*)(void*)>(0x00483F18)(p);}} cleanup{options};
        options[4]=1; // The same inventory context used by CUIItem.
        reinterpret_cast<void(__thiscall*)(void*,int,int,void*,void*,int,int,int,int)>(0x008F5B20)
            (state.tooltip,state.windowX+x+16,state.windowY+y+18,native,options,0,0,0,0);
    }catch(...){static unsigned errors=0;if(errors++<8)CrashReporter::RecordEvent("wardrobe.tooltip","native item tooltip failed id=%d",item.id);TooltipClear();}
}
void __fastcall Mouse(void*,void*,unsigned message,unsigned,int x,int y){
    if(!state.shown || state.deferredClose)return;try{
        int hit=Hit(x,y);
        if(message==WM_LBUTTONDOWN){
            Focus(true);state.pressed=hit;state.clickUid=hit>=1000&&hit<4072?state.model.current.items[(hit-1000)/1024][(hit-1000)%1024].uid:0;
            if(hit<0){Edit(-1);state.dropdown=-1;if(y>=0 && y<23 && state.dialog==Dialog::None){state.dragging=true;state.dragX=x;state.dragY=y;}}Invalidate();
        }
        else if(message==WM_LBUTTONUP){state.dragging=false;int pressed=state.pressed;state.pressed=-1;if(pressed>=0 && pressed==hit)Activate(hit);Invalidate();}
        else if(message==WM_LBUTTONDBLCLK && hit>=1000 && hit<4072 && !state.pending){
            auto uid=state.model.current.items[(hit-1000)/1024][(hit-1000)%1024].uid;
            if(!uid || uid!=state.clickUid)return;
            state.clickUid=0;state.pressed=-1;Activate(hit);if(state.page==0)PreviewSelected();else if(state.page==2)TransferSelected();
        }
    }catch(...){Fail("mouse");}
}
int __fastcall MouseMove(void*,void*,int x,int y){try{
    if(!state.shown || state.deferredClose)return 0;
    if(state.dragging){
        if(!(GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON)?VK_RBUTTON:VK_LBUTTON)&0x8000))state.dragging=false;
        else {auto layer=*reinterpret_cast<void**>(state.window+0x18);if(layer){
            int nx=(std::max)(0,(std::min)(Client::m_nGameWidth-Width,state.windowX+x-state.dragX));
            int ny=(std::max)(0,(std::min)(Client::m_nGameHeight-Height,state.windowY+y-state.dragY));VARIANT empty{};empty.vt=VT_ERROR;empty.scode=DISP_E_PARAMNOTFOUND;
            Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(layer,0x90)(layer,nx,ny,empty,empty));
            state.windowX=nx;state.windowY=ny;TooltipClear();state.hover=-1;return 0;
        }}
    }
    Hover(Hit(x,y),x,y);
}catch(...){Fail("hover");}return 0;}
int __fastcall Wheel(void*,void*,int x,int y,int delta){
    // IUIMsgHandler::OnMouseWheel(x, y, delta), confirmed against CWnd forwarding.
    if(!state.shown || state.pending || state.deferredClose)return 0;try{
        if(state.dialog==Dialog::None && state.page!=1){int source=state.page==2?(x<390?1:0):state.source;int columns=state.page==2?9:WardrobeLayout::GridColumns;auto count=Filter(source,state.page==2&&source==1).size();int last=(std::max)(0,(int(count)-1)/(columns*WardrobeLayout::GridRows));state.gridPage[source]=(std::max)(0,(std::min)(last,state.gridPage[source]+(delta<0?1:-1)));ResetSelection();}
        Invalidate();
    }catch(...){Fail("wheel");}return 1;
}
void __fastcall MouseEnter(void*,void*,int entered){if(!entered){state.hover=state.pressed=-1;TooltipClear();Invalidate();}}
void InsertText(const std::wstring& text){
    if(state.deferredClose)return;
    if(state.pending || (state.edit!=8 && state.edit!=450))return;
    auto& value=state.edit==8?state.search:state.outfitName;auto limit=state.edit==8?32u:8u;
    for(wchar_t c:text)if(c>=32 && c!=127 && !(c>=0xd800 && c<=0xdfff) && value.size()<limit)value+=c;
    if(state.edit==8){std::fill(std::begin(state.gridPage),std::end(state.gridPage),0);ResetSelection();}Invalidate();
}
void __fastcall OnIME(void*,void*,const char* text){try{if(text && strnlen(text,128)<128)InsertText(Wide(text));}catch(...){Fail("ime");}}
void __fastcall Key(void*,void*,unsigned key,unsigned flags){
    if(!state.shown || state.deferredClose || flags&0x80000000u)return;
    try{
        if(key==VK_ESCAPE){if(state.dropdown>=0){state.dropdown=-1;Invalidate();}else if(state.edit>=0){Edit(-1);Invalidate();}else if(state.dialog!=Dialog::None)Activate(400);else Activate(1);return;}
        if(state.edit<0 && WardrobeKeybind::Matches(key,flags)){if(!(flags&0x40000000u))Activate(1);return;}
        if(state.edit<0){
            if(!state.pending && !state.opening && state.page!=2 && state.dialog==Dialog::None)WardrobePreview::Key(key,flags);
            return;
        }
        if(state.pending)return;
        if(key==VK_RETURN){if(state.edit==450 && (state.dialog==Dialog::Save || state.dialog==Dialog::Rename))Activate(401);else{Edit(-1);Invalidate();}return;}
        if(key==VK_BACK){auto& text=state.edit==8?state.search:state.outfitName;if(!text.empty())text.pop_back();if(state.edit==8)ResetSelection();Invalidate();return;}
        if(key==VK_PROCESSKEY || GetKeyState(VK_CONTROL)&0x8000 || GetKeyState(VK_MENU)&0x8000)return;
        auto manager=*reinterpret_cast<unsigned char**>(0x00BEC20C);HWND hwnd=manager?*reinterpret_cast<HWND*>(manager+0xac):nullptr;
        HIMC imc=hwnd?ImmGetContext(hwnd):nullptr;bool composing=false;
        if(imc){DWORD mode=0,sentence=0;composing=ImmGetOpenStatus(imc) && ImmGetConversionStatus(imc,&mode,&sentence) && (mode&IME_CMODE_NATIVE);ImmReleaseContext(hwnd,imc);}if(composing)return;
        BYTE keys[256]{};wchar_t chars[8]{};GetKeyboardState(keys);int n=ToUnicode(key,(flags>>16)&255,keys,chars,8,0);if(n>0)InsertText(std::wstring(chars,n));
    }catch(...){Fail("key");}
}
void __fastcall RootKey(void* handler,void* edx,unsigned key,unsigned flags){
    WardrobePreview::ExternalScope field;
    if(FinishDeferredClose())return;
    if(state.shown || WardrobeInput::Locked()){Key(nullptr,nullptr,key,flags);return;}
    if(WardrobeInput::ConsumeAfterClose(key,flags))return;
    // OnKey receives CWndMan's IUIMsgHandler subobject (manager+4), not its base.
    // Read focus from the singleton, as native CWnd::IsFocused (9E06C5) does.
    auto manager=*reinterpret_cast<unsigned char**>(0x00BEC20C);
    const bool modal=*reinterpret_cast<void**>(0x00BE7910)!=nullptr;
    const bool fieldFocus=manager && *reinterpret_cast<void**>(manager+0x88)==manager+4 && !modal;
    const bool matched=WardrobeKeybind::Matches(key,flags);
    if((key==VK_F8 || matched) && !(flags&0xc0000000u)){
        static unsigned records=0;
        if(records<8){++records;CrashReporter::RecordEvent("wardrobe.key","mapped=%d field=%d focus=%d modal=%d opening=%d",matched,state.field,fieldFocus,modal,state.opening);}
    }
    if(state.field && fieldFocus && state.edit<0 && matched){if(!(flags&0xc0000000u))try{if(!state.opening)OpenWindow();}catch(...){Fail("open");}return;}
    rootKey(handler,edx,key,flags);
}
int __fastcall RootMouse(void* manager,void* edx,unsigned message,unsigned wparam,int lparam){
    WardrobePreview::ExternalScope field;
    if(FinishDeferredClose())return 0;
    if(!WardrobeInput::Locked())return rootMouse(manager,edx,message,wparam,lparam);
    if(!state.shown || !state.ready || state.deferredClose)return 0;
    Cursor();
    POINT point{};
    // Keep native cursor coordinates/timing even though field clicks are consumed.
    reinterpret_cast<void(__thiscall*)(void*,POINT*,int)>(0x009E311B)(manager,&point,0);
    *reinterpret_cast<POINT*>(static_cast<unsigned char*>(manager)+0x9c)=point;
    *reinterpret_cast<DWORD*>(0x00BF1614)=timeGetTime();
    int x=point.x-state.windowX,y=point.y-state.windowY;
    if(message==WM_MOUSEMOVE)MouseMove(nullptr,nullptr,x,y);
    else if(message==WM_MOUSEWHEEL)Wheel(nullptr,nullptr,x,y,short(HIWORD(wparam)));
    else Mouse(nullptr,nullptr,message,wparam,x,y);
    return 0;
}
void __fastcall Draw(void* window,void*,const RECT* rect){if(state.deferredClose)return;try{DrawBody(window,rect);}catch(...){Fail("draw");}}
int __fastcall WindowHitTest(void* window,void*,int x,int y,void** child){
    if(child)*child=nullptr;if(!state.ready || !state.shown || state.deferredClose)return 0;
    return reinterpret_cast<int(__thiscall*)(void*,int,int,void**)>(0x00424461)(window,x,y,child);
}
void __fastcall Update(void*,void*){}
int __fastcall Noop4(void*,void*,int,int,int,int){return 0;}
void __fastcall OnCreate(void*,void*,void*){}
void __fastcall OnButton(void*,void*,unsigned){}
int __fastcall OnFocus(void*,void*,int focused){if(!focused && state.edit>=0)Edit(-1);return !focused || state.ready&&state.shown?1:0;}
void __fastcall SetShow(void*,void*,int show){try{Show(show!=0);}catch(...){Fail("show");}}
int __fastcall IsShown(void*,void*){return state.shown?1:0;}
const void* __fastcall RTTI(void*,void*){return nullptr;}
int __fastcall KindOf(void*,void*,const void*){return 0;}
void* __fastcall RefDestructor(void* ref,void*,unsigned){return static_cast<unsigned char*>(ref)-8;}
void Tables(){
    const DWORD main[]={0x009E067E,0x004243FE,0x009DE7FB,0x00A4F19C,0x00424408,0x009DEB57,0x009E00A6,0x0042444E,0x00A4F236,0x00424461,0x009E03A6,0x00A4F291,0x00424403,0x004244B1};
    const DWORD ui[]={0x00A4F261,0x009E0369,0x00424443,0x00424446,0x009E02AE,0x009E01C8,0x004286F7,0x004286FA,0x004286FD,0x00428701,0x00428704,0x009E03C5,0x009E0447,0x00428708,0x00428709,0x0042870C,0x0042870F,0x00A4F091,0x00A4F097};
    std::memcpy(state.mainTable,main,sizeof(main));std::memcpy(state.uiTable,ui,sizeof(ui));
    state.mainTable[0]=(DWORD)&Update;state.mainTable[1]=(DWORD)&Noop4;state.mainTable[3]=(DWORD)&OnCreate;state.mainTable[8]=(DWORD)&OnButton;
    state.mainTable[9]=(DWORD)&WindowHitTest;state.mainTable[11]=(DWORD)&Draw;
    state.uiTable[0]=(DWORD)&Key;state.uiTable[1]=(DWORD)&OnFocus;state.uiTable[2]=(DWORD)&Mouse;state.uiTable[3]=(DWORD)&MouseMove;state.uiTable[4]=(DWORD)&Wheel;
    state.uiTable[5]=(DWORD)&MouseEnter;state.uiTable[9]=(DWORD)&SetShow;state.uiTable[10]=(DWORD)&IsShown;state.uiTable[15]=(DWORD)&OnIME;
    state.uiTable[17]=(DWORD)&RTTI;state.uiTable[18]=(DWORD)&KindOf;state.refTable[0]=(DWORD)&RefDestructor;
}
void PreloadWardrobeSkin(){
    for(const wchar_t* key:{L"backgrnd",L"modal",L"cell",L"select",L"tabOn",L"tabOff",L"categoryOn",L"categoryOff",L"scene/0",L"scene/1",L"scene/2"})Canvas(root+key);
    for(int width:{48,65,80,96})for(const wchar_t* variant:{L"normal",L"pressed",L"disabled",L"mouseOver"})Canvas(root+L"button"+std::to_wstring(width)+L"/"+variant+L"/0");
    Canvas(L"UI/Basic.img/BtClose/normal/0");
    for(const wchar_t* name:{L"BtOK",L"BtCancel"})for(const wchar_t* variant:{L"normal",L"pressed",L"disabled",L"mouseOver"})Canvas(std::wstring(L"UI/Basic.img/")+name+L"/"+variant+L"/0");
}
void CreateWardrobeWindow(){
    if(!state.window){state.window=new unsigned char[0x100]{};reinterpret_cast<void*(__thiscall*)(void*)>(0x009DE383)(state.window);}
    *reinterpret_cast<DWORD*>(state.window)=(DWORD)state.mainTable;*reinterpret_cast<DWORD*>(state.window+4)=(DWORD)state.uiTable;*reinterpret_cast<DWORD*>(state.window+8)=(DWORD)state.refTable;
    DWORD path=0;auto name=root+L"backgrnd";reinterpret_cast<DWORD*(__thiscall*)(DWORD*,const wchar_t*)>(0x00403382)(&path,name.c_str());
    reinterpret_cast<void(__thiscall*)(void*,DWORD,int,int)>(0x009E0AB2)(state.window,path,0,0);if(!*reinterpret_cast<void**>(state.window+0x68))throw E_POINTER;
    state.windowX=(std::max)(0,(Client::m_nGameWidth-Width)/2);state.windowY=(std::max)(0,(Client::m_nGameHeight-Height)/2);
    reinterpret_cast<void(__thiscall*)(void*,int,int,int,int,int,int,void*,int)>(0x009DE4D2)(state.window,state.windowX,state.windowY,Width,Height,10,1,nullptr,0);
    state.ready=true;Show(true);Focus(true);
}
void OpenWindow(){
    if(!state.field || state.opening || state.shown || state.deferredClose)return;state.status.clear();state.model=Model{};
    WardrobePreview::Stop();WardrobePreview::SetScene(0);
    state.page=state.source=state.category=0;std::fill(std::begin(state.gridPage),std::end(state.gridPage),0);state.search.clear();state.preset=state.menuPreset=state.applyingPreset=-1;state.applyDraft.clear();
    PreloadWardrobeSkin();
    WardrobeInput::Lock();
    state.opening=Send(Open);if(state.opening)CreateWardrobeWindow();else WardrobeInput::Unlock();
}
void RebuildAvatar(){
    state.avatarDirty=false;if(!state.ready || !state.shown || state.opening)return;
    unsigned char look[0x1c5]{};reinterpret_cast<void(__thiscall*)(void*,const void*)>(0x00451541)(look,state.look);
    if(!state.model.original)for(const auto& e:state.model.draft){
        int slot=-e.first-100;auto item=state.model.Find(e.second);int cash=item?item->id:0;
        auto base=state.model.Worn(short(-slot));int normal=base?base->id:0;
        if(slot==11)std::memcpy(look+0x15,&cash,4);
        else {int visible=cash?cash:normal,hidden=cash?normal:0;std::memcpy(look+0x19+slot*4,&visible,4);std::memcpy(look+0xe9+slot*4,&hidden,4);}
    }
    if(WardrobePreview::Ready())WardrobePreview::SetLook(look);
    else {
        Object layer;reinterpret_cast<void**(__thiscall*)(void*,void**)>(0x00426604)(state.window,&layer.p);
        if(!layer.p)throw E_POINTER;
        WardrobePreview::Create(layer.p,look);
    }
}

void Tick(void* localUser){
    state.field=true;state.localUser=localUser;WardrobeInput::UpdateFocus();
    if(FinishDeferredClose())return;
    std::deque<Snapshot> received;{std::lock_guard<std::mutex> lock(queueMutex);received.swap(snapshots);}
    for(auto& snapshot:received){
        if(snapshot.outcome==Close || snapshot.outcome==Heartbeat) {
            if(state.ready && snapshot.token==state.model.current.token) {
                if(snapshot.outcome==Close){CloseWindow();continue;}
                state.lastHeartbeatReply=GetTickCount();
            }
            continue;
        }
        // An OPEN response may arrive after the user closed the waiting window.
        // Release that exact server token without reopening or applying stale data.
        if(!state.ready){if(snapshot.outcome==Open)Send(Close,{},false,snapshot.token);continue;}
        if(snapshot.request!=state.pendingRequest)continue;
        if(!state.opening && snapshot.token!=state.model.current.token)continue;
        if(!state.pending && snapshot.revision<=state.model.current.revision)continue;
        if(snapshot.busy){if(state.opening)Send(Close,{},false,snapshot.token);CloseWindow();continue;}
        bool applied=state.pending && state.pendingAction==Apply && snapshot.outcome==Apply;
        for(auto& source:snapshot.items)for(auto& item:source){item.name=ItemName(item.id);item.ringEffects=RingEffects(item.id);}
        if(state.dialog==Dialog::Binding)Popup(Dialog::None);
        state.controls.clear();
        bool transferDone=state.page==2 && (snapshot.outcome==Store || snapshot.outcome==Take);
        state.status=snapshot.outcome==state.pendingAction && !transferDone?L"":Wide(snapshot.message);
        state.model.Refresh(std::move(snapshot),applied);state.pending=false;state.opening=false;
        state.model.original=state.page==1;
        if(applied)state.preset=state.applyingPreset;
        state.applyingPreset=-1;state.applyDraft.clear();
        state.lastHeartbeat=state.lastHeartbeatReply=GetTickCount();
        state.preset=CurrentPreset();
        ResetSelection();state.dropdown=-1;
        reinterpret_cast<void(__thiscall*)(void*,const void*)>(0x00451541)(state.look,static_cast<unsigned char*>(localUser)+0x8c);
        state.avatarDirty=true;Invalidate();
    }
    if(!state.shown)return;
    if(WardrobeInput::IsForeground())Cursor();
    DWORD now=GetTickCount();
    if(state.opening && now-state.sentAt>=15000){CloseWindow();return;}
    if(!state.opening && now-state.lastHeartbeatReply>=20000){CloseWindow();return;}
    if(!state.opening && now-state.lastHeartbeat>=5000){state.lastHeartbeat=now;Send(Heartbeat);}
    if(state.pending && !state.opening && state.syncAttempts<3 && now-state.lastSync>=10000){state.lastSync=now;++state.syncAttempts;Send(Sync,{},true);}
    if(state.pending && !state.opening && state.syncAttempts>=3 && now-state.lastSync>=10000){CrashReporter::RecordEvent("wardrobe.window","request_timeout action=%d request=%d",state.pendingAction,state.pendingRequest);CloseWindow();return;}
    if(now-state.lastStatusTick>=1000){state.lastStatusTick=now;if(state.pending)Invalidate();}
    if(state.avatarDirty)RebuildAvatar();
    WardrobePreview::Update(state.page!=2 && state.dialog==Dialog::None,!state.pending && state.edit<0 && state.dropdown<0);
    FinishDeferredClose();
}

}

bool WardrobeWnd::Install(){
    const unsigned char wnd[]={0xb8,0x4a,0x68,0xae,0,0xe8},init[]={0xb8,0x48,0xc0,0xa7,0,0xe8};
    const unsigned char copy[]={0x8b,0xc1,0x8b,0x4c,0x24,0x04},destroy[]={0xb8,0x48,0x6c,0xae,0,0xe8},key[]={0xa1,0xd4,0xde,0xbe,0};
    const unsigned char hit[]={0x56,0x8b,0x74,0x24,0x10,0x57},release[]={0x83,0xa1,0x90,0,0,0,0};
    if(std::memcmp(reinterpret_cast<void*>(0x009DE383),wnd,sizeof(wnd)) || std::memcmp(reinterpret_cast<void*>(0x0045149f),init,sizeof(init))
        || std::memcmp(reinterpret_cast<void*>(0x00451541),copy,sizeof(copy)) || std::memcmp(reinterpret_cast<void*>(0x009E00AF),destroy,sizeof(destroy))
        || std::memcmp(reinterpret_cast<void*>(0x009E2F05),key,sizeof(key)) || std::memcmp(reinterpret_cast<void*>(0x00424461),hit,sizeof(hit))
        || std::memcmp(reinterpret_cast<void*>(0x009E3AC0),release,sizeof(release)) || *reinterpret_cast<DWORD*>(0x00B404E4)!=0x009E067E)return false;
    const unsigned char mouse[]={0xb8,0x60,0x6f,0xae,0,0xe8};
    if(std::memcmp(reinterpret_cast<void*>(0x009E3AE6),mouse,sizeof(mouse)))return false;
    if(!WardrobeInput::Install())return false;
    if(!WardrobePreview::Install()){WardrobeInput::Uninstall();return false;}
    Tables();
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&rootMouse),reinterpret_cast<void*>(&RootMouse))){WardrobePreview::Uninstall();WardrobeInput::Uninstall();return false;}
    state.installed=Memory::SetHook(true,reinterpret_cast<void**>(&rootKey),reinterpret_cast<void*>(&RootKey));
    // Irreversible key template writes come last: there is no later fallible install.
    if(state.installed && !WardrobeKeybind::Install()){
        Memory::SetHook(false,reinterpret_cast<void**>(&rootKey),reinterpret_cast<void*>(&RootKey));state.installed=false;
    }
    if(!state.installed){Memory::SetHook(false,reinterpret_cast<void**>(&rootMouse),reinterpret_cast<void*>(&RootMouse));WardrobePreview::Uninstall();WardrobeInput::Uninstall();}
    else CrashReporter::RecordEvent("wardrobe.install","ready keyAction=40 focusSource=manager-singleton");
    return state.installed;
}
bool WardrobeWnd::HandlePacket(const unsigned char* data,unsigned short length){
    if(!data || length<6 || data[4]!=0x05 || data[5]!=0x10)return false;
    Snapshot snapshot;if(!state.installed)return true;
    if(!Decode(data,length,snapshot)){static volatile LONG errors=0;if(InterlockedIncrement(&errors)<=8)CrashReporter::RecordEvent("wardrobe.packet","invalid snapshot bytes=%u",length);return true;}
    std::lock_guard<std::mutex> lock(queueMutex);if(snapshots.size()>=8)snapshots.pop_front();snapshots.push_back(std::move(snapshot));return true;
}
void WardrobeWnd::OnFieldUpdate(void* user){if(state.installed && user)try{Tick(user);}catch(...){Fail("update");}}
void WardrobeWnd::OnFieldDispose(){state.field=false;state.localUser=nullptr;if(!state.installed)return;{std::lock_guard<std::mutex> lock(queueMutex);snapshots.clear();}try{CloseWindow();}catch(...){Fail("dispose");}}
bool WardrobeWnd::IsEditing(){return state.shown && state.edit>=0;}
