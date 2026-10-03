#include "stdafx.h"
#include "MixedDyeWnd.h"
#include "MixedDyeWindowModel.h"
#include "MixedDyeResources.h"
#include "CrashReporter.h"
#include <map>
#include <mutex>
#include <deque>

namespace {
using namespace MixedDyeResources;
using MixedDyeWindowModel::Snapshot;
struct State {
    bool installed=false,ready=false,shown=false,pending=false,confirm=false,hideHat=false,avatarDirty=false;
    unsigned char* window=nullptr;
    DWORD mainTable[14]{},uiTable[19]{},refTable[1]{},avatars[2][2]{};
    int token=0,coupon=0,original=0,originalSecondary=-1,mask=0,primary=-1,secondary=-1,editing=0,hover=-1,pressed=-1;
    DWORD openedAt=0,sentAt=0;
    unsigned char look[0x1c5]{};
    std::map<std::wstring,Object> canvases,texts;
};
// PCOM can unload before the hook DLL at process shutdown. Release active
// previews on close; keep the bounded asset cache alive until process teardown.
State& state=*new State;
// Network delivery only queues validated snapshots. All PCOM/window/avatar work
// stays on CUserLocal::Update, the same game thread as the original beauty dialog.
std::mutex snapshotMutex;
std::deque<Snapshot> snapshots;
Factory FactoryFn() {return reinterpret_cast<Factory>(GetProcAddress(GetModuleHandleW(L"PCOM.dll"),"PcCreateObject"));}
std::wstring Root() {return state.coupon==5152302?L"UI/MixedDyeLens.img/UtilDlgEx_MixLens_New/":L"UI/MixedDyeHair.img/UtilDlgEx_MixHair_New/";}
void* Canvas(const std::wstring& path) {
    auto found=state.canvases.find(path);if(found!=state.canvases.end())return found->second.p;
    auto manager=*reinterpret_cast<void**>(0x00BF14E8);if(!manager)throw E_POINTER;
    Value value;VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    BSTR name=SysAllocString(path.c_str());if(!name)throw E_OUTOFMEMORY;
    auto hr=Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT,VARIANT,VARIANT*)>(manager,0x1c)(manager,name,missing,missing,&value.v);
    SysFreeString(name);Check(hr);auto canvas=Query(value.v,CanvasIID());if(!canvas.p)throw E_NOINTERFACE;
    return state.canvases.emplace(path,std::move(canvas)).first->second.p;
}
void Copy(void* dst,void* src,int x,int y) {
    Check(Method<HRESULT(__stdcall*)(void*,int,int,void*,VARIANT)>(dst,0x80)(dst,x,y,src,Integer(255)));
}
void Image(void* dst,const std::wstring& path,int dx=0,int dy=0,bool origin=true) {
    auto src=Canvas(path);Copy(dst,src,dx-(origin?Int(src,0x6c):0),dy-(origin?Int(src,0x74):0));
}
// UI text is Unicode, rasterized once with GDI and cached as PCOM canvases. This
// avoids sending GBK bytes through the client's unrelated NPC text formatter.
Object TextCanvas(const std::wstring& text,unsigned color) {
    constexpr int w=250,h=17;
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)throw E_OUTOFMEMORY;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;
    info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    // Match the original UI's 12-pixel bitmap labels; grayscale smoothing makes
    // these dynamic lines look different from the adjacent resource text.
    HFONT font=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,GB2312_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"SimSun");
    if(!bitmap || !font){if(bitmap)DeleteObject(bitmap);if(font)DeleteObject(font);DeleteDC(dc);throw E_OUTOFMEMORY;}
    auto oldBitmap=SelectObject(dc,bitmap),oldFont=SelectObject(dc,font);
    struct Cleanup { HDC dc; HBITMAP bitmap; HFONT font; HGDIOBJ oldBitmap,oldFont;
        ~Cleanup(){SelectObject(dc,oldBitmap);SelectObject(dc,oldFont);DeleteObject(bitmap);DeleteObject(font);DeleteDC(dc);}
    } cleanup{dc,bitmap,font,oldBitmap,oldFont};
    std::memset(bits,0,w*h*4);SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
    RECT rect{0,0,w,h};DrawTextW(dc,text.c_str(),-1,&rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);GdiFlush();
    auto out=New(FactoryFn(),L"Canvas",CanvasIID());
    Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(out.p,0x2c)(out.p,w,h,Integer(0),Integer(2)));
    Check(Method<HRESULT(__stdcall*)(void*,LONG)>(out.p,0x54)(out.p,2));
    Object raw;Check(Method<HRESULT(__stdcall*)(void*,int,int,void**)>(out.p,0x34)(out.p,0,0,&raw.p));
    LONG pitch=0;Value address;Check(Method<HRESULT(__stdcall*)(void*,LONG*,VARIANT*)>(raw.p,0x1c)(raw.p,&pitch,&address.v));
    struct Unlock{void* p;RECT r{0,0,w,h};~Unlock(){Method<HRESULT(__stdcall*)(void*,RECT*)>(p,0x20)(p,&r);}} unlock{raw.p};
    if(address.v.vt!=(VT_BYREF|VT_UI4) || !address.v.byref || pitch<w*4 || pitch>16384)throw E_INVALIDARG;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const unsigned source=static_cast<unsigned*>(bits)[y*w+x];
        const unsigned alpha=(std::max)({source&255,(source>>8)&255,(source>>16)&255});
        const unsigned pixel=alpha<<24 | (color&0xffffff);
        std::memcpy(static_cast<unsigned char*>(address.v.byref)+y*pitch+x*4,&pixel,4);
    }
    return out;
}
void Text(void* dst,const std::wstring& text,int x,int y,unsigned color=0xffffff) {
    const auto key=std::to_wstring(color)+L":"+text;auto found=state.texts.find(key);
    if(found==state.texts.end()) {if(state.texts.size()>64)state.texts.clear();found=state.texts.emplace(key,TextCanvas(text,color)).first;}
    Copy(dst,found->second.p,x,y);
}
const wchar_t* ColorName(int color) {
    static const wchar_t* hair[]={L"黑色",L"红色",L"橙色",L"黄色",L"绿色",L"蓝色",L"紫色",L"褐色"};
    static const wchar_t* face[]={L"黑色",L"蓝色",L"红色",L"绿色",L"褐色",L"祖母绿",L"紫色",L"紫水晶"};
    return color>=0 && color<8?(state.coupon==5152302?face[color]:hair[color]):L"未选择";
}
void Invalidate() {if(state.ready)reinterpret_cast<void(__thiscall*)(void*,const RECT*)>(0x009E04C9)(state.window,nullptr);}
void Focus(bool focused) {
    auto manager=*reinterpret_cast<unsigned char**>(0x00BEC20C);if(!manager || !state.window)return;
    if(focused || *reinterpret_cast<void**>(manager+0x88)==state.window+4)
        reinterpret_cast<void(__thiscall*)(void*,void*)>(0x009E3264)(manager,focused?state.window+4:manager+4);
}
void Show(bool visible) {
    state.shown=visible;if(!state.ready)return;
    for(int offset:{0x18,0x1c,0x20}) {auto layer=*reinterpret_cast<void**>(state.window+offset);
        if(layer)Check(Method<HRESULT(__stdcall*)(void*,int)>(layer,0x11c)(layer,visible?1:0));}
    if(visible)Invalidate();
}
void ReleaseAvatars() {
    for(auto& avatar:state.avatars)if(avatar[1]) {
        reinterpret_cast<void(__thiscall*)(DWORD*,int)>(0x00428C15)(avatar,0);avatar[0]=avatar[1]=0;
    }
}
void DestroyDyeWindow() {
    // CWndMan::GetWndFromPoint still visits hidden CWnd rectangles. Closing must
    // remove native registration, not just hide its three graphics layers.
    state.shown=false;
    Focus(false);
    auto manager=*reinterpret_cast<unsigned char**>(0x00BEC20C);
    if(manager && state.window && *reinterpret_cast<void**>(manager+0x90)==state.window+4)
        reinterpret_cast<void(__thiscall*)(void*)>(0x009E3AC0)(manager);
    ReleaseAvatars();
    if(state.ready) {
        state.ready=false; // Native teardown can call back into our UI handlers.
        reinterpret_cast<void(__thiscall*)(void*)>(0x009E00AF)(state.window);
    }
}
bool Send(int action) {
    if(!state.token)return false;
    unsigned char data[12]{};MixedDyeWindowModel::Action(data,action,state.token,state.primary,state.secondary);
    struct Packet{int loopback;unsigned char* data;unsigned long size;unsigned offset;int shanda;} packet{0,data,sizeof(data),0,0};
    auto socket=*reinterpret_cast<void**>(0x00BE7914);if(!socket)return false;
    reinterpret_cast<void(__thiscall*)(void*,Packet*)>(0x0049637B)(socket,&packet);return true;
}
void Close(bool cancel=true) {
    state.shown=false;
    if(cancel && state.token)try{Send(0);}catch(...){CrashReporter::RecordEvent("mixedDye.window","cancel_send_failed");}
    state.token=0;state.pending=false;state.confirm=false;
    state.avatarDirty=false;state.hover=state.pressed=-1;state.texts.clear();
    DestroyDyeWindow();
}
void Fail(const char* phase) {
    CrashReporter::RecordEvent("mixedDye.window", "failed stage=%s token=%d",phase,state.token);
    try{Close();}catch(...) {state.token=0;state.shown=false;}
}
bool Contains(int x,int y,int left,int top,int w,int h){return x>=left && x<left+w && y>=top && y<top+h;}
int __fastcall WindowHitTest(void* window,void*,int x,int y,void** child) {
    if(child)*child=nullptr;
    if(!state.ready || !state.shown || !state.token)return 0;
    return reinterpret_cast<int(__thiscall*)(void*,int,int,void**)>(0x00424461)(window,x,y,child);
}
int Hit(int x,int y) {
    if(!state.ready || !state.shown || !state.token)return -1;
    if(Contains(x,y,347,5,12,12))return 22;
    if(Contains(x,y,315,232,40,16))return 21;
    if(state.pending)return -1;
    if(Contains(x,y,273,232,40,16))return 20;
    if(Contains(x,y,156,232,55,16))return 23;
    if(state.editing>=0)for(int color=0;color<8;++color) {
        auto canvas=Canvas(Root()+L"BtColor/button:BtColor"+std::to_wstring(color)+L"/normal/0");
        if(Contains(x,y,-Int(canvas,0x6c)+state.editing*278,-Int(canvas,0x74),Int(canvas,0x40),Int(canvas,0x48)))return color;
    }
    if(Contains(x,y,19,136,52,88))return 24;
    if(Contains(x,y,297,136,52,88))return 25;
    return -1;
}
const wchar_t* ButtonState(int id,bool enabled=true) {
    return !enabled?L"disabled":state.pressed==id?L"pressed":state.hover==id?L"mouseOver":L"normal";
}
void DrawBody(void* window,const RECT* rect) {
    reinterpret_cast<void(__thiscall*)(void*,const RECT*)>(0x009E0502)(window,rect);
    DWORD canvas=0;reinterpret_cast<DWORD*(__thiscall*)(void*,DWORD*)>(0x00425C4C)(window,&canvas);
    Object owned(reinterpret_cast<void*>(canvas));if(!canvas)return;auto dst=owned.p;
    auto root=Root();for(const wchar_t* name:{L"backgrnd2",L"backgrnd3",L"backgrnd4",L"backgrnd5",L"layer:shadow"})Image(dst,root+name);
    DWORD icon=0;auto itemInfo=*reinterpret_cast<void**>(0x00BE78D8);
    if(itemInfo){reinterpret_cast<DWORD*(__thiscall*)(void*,DWORD*,int,int,int)>(0x005D3BD8)(itemInfo,&icon,state.coupon,1,0);Object image(reinterpret_cast<void*>(icon));if(icon)Copy(dst,image.p,32,70);}
    if(state.pending)Text(dst,L"正在应用混染，请稍候。",97,51);
    else if(state.confirm) {
        Text(dst,state.coupon==5152302?L"确认使用 1 张均衡混合美瞳券？":L"确认使用 1 张均衡混合染色券？",97,40);
        Text(dst,std::wstring(ColorName(state.primary))+L" 50% + "+ColorName(state.secondary)+L" 50%",97,62);
        Text(dst,L"再次点击确认应用，取消保留原外观。",97,84);
    } else {
        Text(dst,state.editing==1?L"请选择混合色。":state.editing==0?L"请选择基本色。":L"请确认右侧的混染效果。",97,39);
        Text(dst,L"两种颜色各占 50%。",97,58);
        Text(dst,L"点击色块可以重新选择。",97,77);
        Text(dst,state.mask==255?L"预览和取消不消耗道具。":L"灰色选项暂无可用颜色资源。",97,96);
    }
    for(int bank=0;bank<2;++bank) {
        if(state.editing==bank)for(int color=0;color<8;++color) {
            const int other=bank?state.primary:state.secondary;
            auto suffix=ButtonState(color,!state.pending && MixedDyeWindowModel::Selectable(state.mask,color,other));
            Image(dst,root+L"BtColor/button:BtColor"+std::to_wstring(color)+L"/"+suffix+L"/0",278*bank);
        } else {
            const int color=bank?state.secondary:state.primary;
            if(color>=0)Image(dst,root+(state.coupon==5152302?L"LensColor/":L"HairColor/")+std::to_wstring(color),27+bank*278,152,false);
            Text(dst,L"50",30+bank*278,202,0xffffff);
        }
    }
    const bool ready=MixedDyeWindowModel::Ready(state.mask,state.primary,state.secondary);
    Image(dst,root+L"button:BtOK/"+ButtonState(20,ready&&!state.pending)+L"/0");
    Image(dst,root+L"button:BtCancel/"+ButtonState(21)+L"/0",315,232,false);
    Image(dst,root+L"button:BtClose/"+ButtonState(22)+L"/0",347,5,false);
    Image(dst,root+(state.hideHat?L"button:BtOff/":L"button:BtOn/")+ButtonState(23,!state.pending)+L"/0");
}
void __fastcall Draw(void* window,void*,const RECT* rect){try{DrawBody(window,rect);}catch(...){Fail("draw");}}
void Select(int id) {
    if(id==21 || id==22){Close();return;}if(state.pending)return;
    if(id==23){state.hideHat=!state.hideHat;state.avatarDirty=true;}
    else if(id==24 || id==25){state.editing=id-24;state.confirm=false;}
    else if(id>=0 && id<8 && state.editing>=0) {
        const int other=state.editing?state.primary:state.secondary;
        if(!MixedDyeWindowModel::Selectable(state.mask,id,other))return;
        if(state.editing==0){state.primary=id;state.editing=state.secondary<0?1:-1;}
        else {state.secondary=id;state.editing=-1;}
        state.confirm=false;state.avatarDirty=true;
    } else if(id==20 && MixedDyeWindowModel::Ready(state.mask,state.primary,state.secondary)) {
        if(state.confirm){if(Send(1)){state.pending=true;state.sentAt=GetTickCount();}else Close();}
        else {state.confirm=true;state.editing=-1;}
    }
    Invalidate();
}
void __fastcall Mouse(void*,void*,unsigned message,unsigned,int x,int y) {
    if(!state.ready || !state.shown || !state.token)return;
    try {
        int hit=Hit(x,y);if(message==WM_LBUTTONDOWN){state.pressed=hit;Focus(true);Invalidate();}
        else if(message==WM_LBUTTONUP){int pressed=state.pressed;state.pressed=-1;if(pressed>=0 && pressed==hit)Select(hit);Invalidate();}
    }catch(...){Fail("mouse");}
}
int __fastcall MouseMove(void*,void*,int x,int y){if(!state.ready || !state.shown || !state.token)return 0;try{int hit=Hit(x,y);if(hit!=state.hover){state.hover=hit;Invalidate();}}catch(...){Fail("hover");}return 0;}
void __fastcall MouseEnter(void*,void*,int entered){if(!entered){state.hover=state.pressed=-1;Invalidate();}}
void __fastcall Key(void*,void*,unsigned key,unsigned flags){
    if(!state.shown) {auto manager=*reinterpret_cast<unsigned char**>(0x00BEC20C);if(manager)reinterpret_cast<void(__thiscall*)(void*,unsigned,unsigned)>(0x009E2F05)(manager+4,key,flags);return;}
    if(flags&0x80000000U)return;
    try{if(key==VK_ESCAPE){if(state.confirm){state.confirm=false;Invalidate();}else Close();}}catch(...){Fail("key");}
}
void __fastcall Update(void*,void*){}
int __fastcall Noop4(void*,void*,int,int,int,int){return 0;}
void __fastcall OnCreate(void*,void*,void*){}
void __fastcall OnButton(void*,void*,unsigned){}
int __fastcall OnFocus(void*,void*,int focused){return !focused || (state.ready && state.shown && state.token)?1:0;}
void __fastcall SetShow(void*,void*,int show){try{Show(show!=0);}catch(...){Fail("show");}}
int __fastcall IsShown(void*,void*){return state.shown?1:0;}
void __fastcall OnIME(void*,void*,const char*){}
const void* __fastcall RTTI(void*,void*){return nullptr;}
int __fastcall KindOf(void*,void*,const void*){return 0;}
void* __fastcall RefDestructor(void* ref,void*,unsigned){return static_cast<unsigned char*>(ref)-8;}
void Tables() {
    const DWORD main[]={0x009E067E,0x004243FE,0x009DE7FB,0x00A4F19C,0x00424408,0x009DEB57,0x009E00A6,0x0042444E,0x00A4F236,0x00424461,0x009E03A6,0x00A4F291,0x00424403,0x004244B1};
    const DWORD ui[]={0x00A4F261,0x009E0369,0x00424443,0x00424446,0x009E02AE,0x009E01C8,0x004286F7,0x004286FA,0x004286FD,0x00428701,0x00428704,0x009E03C5,0x009E0447,0x00428708,0x00428709,0x0042870C,0x0042870F,0x00A4F091,0x00A4F097};
    std::memcpy(state.mainTable,main,sizeof(main));std::memcpy(state.uiTable,ui,sizeof(ui));
    state.mainTable[0]=(DWORD)&Update;state.mainTable[1]=(DWORD)&Noop4;state.mainTable[3]=(DWORD)&OnCreate;
    state.mainTable[8]=(DWORD)&OnButton;state.mainTable[11]=(DWORD)&Draw;
    state.mainTable[9]=(DWORD)&WindowHitTest;
    state.uiTable[0]=(DWORD)&Key;state.uiTable[1]=(DWORD)&OnFocus;state.uiTable[2]=(DWORD)&Mouse;state.uiTable[3]=(DWORD)&MouseMove;
    state.uiTable[5]=(DWORD)&MouseEnter;state.uiTable[9]=(DWORD)&SetShow;state.uiTable[10]=(DWORD)&IsShown;
    state.uiTable[15]=(DWORD)&OnIME;state.uiTable[17]=(DWORD)&RTTI;state.uiTable[18]=(DWORD)&KindOf;state.refTable[0]=(DWORD)&RefDestructor;
}
void CreateDyeWindow() {
    // Reuse the CWnd object, but unregister old layers before changing hair/eye backgrounds.
    if(state.ready)DestroyDyeWindow();
    if(!state.window){state.window=new unsigned char[0x100]{};reinterpret_cast<void*(__thiscall*)(void*)>(0x009DE383)(state.window);}
    *reinterpret_cast<DWORD*>(state.window)=(DWORD)state.mainTable;*reinterpret_cast<DWORD*>(state.window+4)=(DWORD)state.uiTable;*reinterpret_cast<DWORD*>(state.window+8)=(DWORD)state.refTable;
    DWORD path=0;auto name=Root()+L"backgrnd";reinterpret_cast<DWORD*(__thiscall*)(DWORD*,const wchar_t*)>(0x00403382)(&path,name.c_str());
    reinterpret_cast<void(__thiscall*)(void*,DWORD,int,int)>(0x009E0AB2)(state.window,path,0,0);
    if(!*reinterpret_cast<void**>(state.window+0x68))throw E_POINTER;
    reinterpret_cast<void(__thiscall*)(void*,int,int,int,int,int,int,void*,int)>(0x009DE4D2)(state.window,
        (std::max)(0,(Client::m_nGameWidth-368)/2),(std::max)(0,(Client::m_nGameHeight-259)/2),368,259,10,1,nullptr,0);
    state.ready=true;
}
void Avatars() {
    ReleaseAvatars();
    const bool face=state.coupon==5152302;
    for(int i=0;i<2;++i) {
        unsigned char look[0x1c5]{};
        reinterpret_cast<void(__thiscall*)(void*,const void*)>(0x00451541)(look,state.look);
        int appearance=MixedDye::Encode(state.original,state.originalSecondary,face);
        if(i==1 && state.primary>=0)appearance=MixedDye::Encode(MixedDye::WithColor(state.original,state.primary,face),state.secondary,face);
        std::memcpy(look+(face?0x11:0x19),&appearance,4);
        // Same weapon suppression as the native beauty preview. Hats are optional.
        for(int offset:{0x15,0x45,0x115})std::memset(look+offset,0,4);
        if(state.hideHat)for(int offset:{0x1d,0xed})std::memset(look+offset,0,4);
        auto& avatar=state.avatars[i];reinterpret_cast<void(__thiscall*)(DWORD*)>(0x00428967)(avatar);
        Object layer;reinterpret_cast<void**(__thiscall*)(void*,void**)>(0x00426604)(state.window,&layer.p);
        if(!layer.p || !avatar[1])throw E_POINTER;
        Object vector;Check(static_cast<IUnknown*>(layer.p)->QueryInterface(VectorIID(),&vector.p));
        // CAvatar::Init consumes its two COM smart-pointer arguments (ret 0x24).
        reinterpret_cast<void(__thiscall*)(void*,const void*,int,void*,void*,int,int,int,int,int)>(0x0045149F)(
            reinterpret_cast<void*>(avatar[1]),look,5,vector.detach(),layer.detach(),1,i?239:121,198,100,0);
    }
    state.avatarDirty=false;
}
void Tick(void* localUser) {
    std::deque<Snapshot> received;
    {std::lock_guard<std::mutex> lock(snapshotMutex);received.swap(snapshots);}
    for(const auto& snapshot:received) {
        if(snapshot.result==0) {
            Close(false);state.token=snapshot.token;state.coupon=snapshot.coupon;state.original=snapshot.original;
            state.originalSecondary=snapshot.secondary;state.mask=snapshot.mask;state.primary=state.secondary=-1;
            state.editing=0;state.hideHat=false;state.openedAt=GetTickCount();
            // CUser contains its native CAvatar at +0x88; AvatarLook is CAvatar+4.
            reinterpret_cast<void(__thiscall*)(void*,const void*)>(0x00451541)(state.look,static_cast<unsigned char*>(localUser)+0x8c);
            CreateDyeWindow();state.avatarDirty=true;Show(true);Focus(true);
        } else if(snapshot.token==state.token){Close(false);}
    }
    if(!state.token)return;
    if(GetTickCount()-state.openedAt>=300000 || state.pending && GetTickCount()-state.sentAt>=10000){Close();return;}
    if(!state.shown)return;
    if(state.avatarDirty)Avatars();
    for(auto& avatar:state.avatars)if(avatar[1])reinterpret_cast<void(__thiscall*)(void*)>(0x004522A6)(reinterpret_cast<void*>(avatar[1]));
}
}
bool MixedDyeWnd::Install() {
    // Signature-gate every native ABI dependency group before enabling the window.
    const unsigned char wnd[]={0xb8,0x4a,0x68,0xae,0,0xe8},init[]={0xb8,0x48,0xc0,0xa7,0,0xe8};
    const unsigned char copy[]={0x8b,0xc1,0x8b,0x4c,0x24,0x04};
    const unsigned char destroy[]={0xb8,0x48,0x6c,0xae,0,0xe8},hit[]={0x56,0x8b,0x74,0x24,0x10,0x57};
    const unsigned char releaseCapture[]={0x83,0xa1,0x90,0,0,0,0};
    if(std::memcmp(reinterpret_cast<void*>(0x009DE383),wnd,sizeof(wnd)) || std::memcmp(reinterpret_cast<void*>(0x0045149f),init,sizeof(init))
        || std::memcmp(reinterpret_cast<void*>(0x00451541),copy,sizeof(copy))
        || std::memcmp(reinterpret_cast<void*>(0x009E00AF),destroy,sizeof(destroy))
        || std::memcmp(reinterpret_cast<void*>(0x00424461),hit,sizeof(hit))
        || std::memcmp(reinterpret_cast<void*>(0x009E3AC0),releaseCapture,sizeof(releaseCapture))
        || *reinterpret_cast<DWORD*>(0x00B404E4)!=0x009E067E)return false;
    Tables();state.installed=true;return true;
}
bool MixedDyeWnd::HandlePacket(const unsigned char* data,unsigned short length) {
    if(!data || length<6 || data[4]!=0x0e || data[5]!=0x10)return false;
    Snapshot snapshot;if(!state.installed || !MixedDyeWindowModel::Decode(data,length,snapshot))return true;
    std::lock_guard<std::mutex> lock(snapshotMutex);
    if(snapshots.size()>=8)snapshots.pop_front();snapshots.push_back(snapshot);return true;
}
void MixedDyeWnd::OnFieldUpdate(void* localUser){if(state.installed && localUser)try{Tick(localUser);}catch(...){Fail("update");}}
void MixedDyeWnd::OnFieldDispose(){if(!state.installed)return;{std::lock_guard<std::mutex> lock(snapshotMutex);snapshots.clear();}try{Close();}catch(...){Fail("dispose");}}
