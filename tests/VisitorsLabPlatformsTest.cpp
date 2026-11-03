#define NOMINMAX
#define VISITORS_LAB_TEST
#include "../ezorsia/VisitorsLabPlatforms.cpp"
#include "VisitorsShuttleTest.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>

using TestSupport::Require;
static std::vector<std::string> diagnosticLines;
static const char* phase="initialization";
namespace ClientLog {
void Append(Component,const char* format,...) {
    char buffer[1024]; va_list args; va_start(args,format);
    vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
    diagnosticLines.emplace_back(buffer);
}
}
static int hooks = 0, failHook = -1;
bool Memory::SetHook(bool attach, void**, void*) {
    if (!attach) { --hooks; return true; }
    if (hooks == failHook) return false;
    ++hooks; return true;
}
namespace {
unsigned char field[0x200]{}, space[0x200]{};
struct Foot { alignas(8) unsigned char data[0x60]{}; };
std::map<int,Foot> feet;
std::map<int,RECT> envelopes;
std::map<std::string,std::array<unsigned,7>> records;
std::vector<Object> layers, origins;
Factory factory;
void* graphics=nullptr;
void* platformCanvas=nullptr;
Object LayerAlpha(void* layer) {
    Object alpha;
    Check(Method<HRESULT(__stdcall*)(void*,void**)>(layer,0xE4)(layer,&alpha.p));
    Require(alpha.p,"native layer alpha vector");return alpha;
}
HRESULT __stdcall NoPresent(void*,const RECT*,const RECT*,HWND,const RGNDATA*) { return S_OK; }
void ReplaceSlot(void** slot,void* value) {
    DWORD old=0;Require(VirtualProtect(slot,4,PAGE_READWRITE,&old)!=0,"test device vtable protection");
    *slot=value;VirtualProtect(slot,4,old,&old);
}
unsigned RenderedPixels() {
    phase="render frame";
    // Read only this test's hidden render target, never the game or desktop.
    for(int i=0;i<3;++i) {
        int next=0;Check(Method<HRESULT(__stdcall*)(void*,int*)>(graphics,0x14)(graphics,&next));
        Check(Method<HRESULT(__stdcall*)(void*,int)>(graphics,0x18)(graphics,next));
        Check(Method<HRESULT(__stdcall*)(void*)>(graphics,0x1C)(graphics));
    }
    auto* device=Field<void*>(graphics,0x90);
    Object target,copy;
    phase="read render target";
    Check(Method<HRESULT(__stdcall*)(void*,void**)>(device,0x80)(device,&target.p));
    struct Desc { unsigned format,type,usage,pool,size,multisample,width,height; } desc{};
    Check(Method<HRESULT(__stdcall*)(void*,Desc*)>(target.p,0x20)(target.p,&desc));
    Require(desc.format==21 || desc.format==22,"32-bit render target");
    Check(Method<HRESULT(__stdcall*)(void*,unsigned,unsigned,unsigned,void**)>(device,0x6C)
        (device,desc.width,desc.height,desc.format,&copy.p));
    RECT area{0,0,LONG(desc.width),LONG(desc.height)};POINT destination{};
    Check(Method<HRESULT(__stdcall*)(void*,void*,const RECT*,unsigned,void*,const POINT*)>(device,0x70)
        (device,target.p,&area,1,copy.p,&destination));
    struct Locked { int pitch;void* bits; } locked{};
    Check(Method<HRESULT(__stdcall*)(void*,Locked*,const RECT*,unsigned)>(copy.p,0x24)(copy.p,&locked,nullptr,0x10));
    unsigned visible=0;
    for(unsigned y=0;y<desc.height;++y) {
        auto* row=reinterpret_cast<unsigned*>(static_cast<char*>(locked.bits)+y*locked.pitch);
        for(unsigned x=0;x<desc.width;++x)if(row[x]&0xFFFFFF)++visible;
    }
    Check(Method<HRESULT(__stdcall*)(void*)>(copy.p,0x28)(copy.p));
    return visible;
}
int clears = 0, loads = 0, updates = 0;
void __fastcall UpdateStub(void*,void*) { ++updates; }
void* __fastcall Find(void* map,void*,const char** key,void*) {
    Require(map == field+0xA0,"native object lookup uses field table");
    auto found = records.find(*key);
    return found == records.end() ? nullptr : found->second.data();
}
void* __fastcall InsertStub(void*,void*,int x1,int y1,int x2,int y2,void* ref) {
    auto* fh=Field<void*>(ref,4);
    envelopes[Field<int>(fh,0x48)]={x1,y1,x2,y2}; return nullptr;
}
void __fastcall ClearStub(void*,void*) { ++clears; }
void __fastcall LoadStub(void*,void*,int mapId) {
    ++loads; if(mapId!=502023000)return;
    feet.clear(); records.clear(); layers.clear(); origins.clear(); envelopes.clear();
    auto root=Field<void*>(field,0x30);
    auto fhs=Child(root,L"foothold");
    for(const auto& page:Names(fhs.p)) {
        auto a=Child(fhs.p,page);
        for(const auto& group:Names(a.p)) {
            auto b=Child(a.p,group);
            for(const auto& key:Names(b.p)) {
                auto entry=Child(b.p,key); int sn=std::stoi(key);
                auto* fh=feet[sn].data;
                Field<int>(fh,0x48)=sn;
                Field<int>(fh,0x0C)=Number(entry.p,L"x1"); Field<int>(fh,0x10)=Number(entry.p,L"y1");
                Field<int>(fh,0x14)=Number(entry.p,L"x2"); Field<int>(fh,0x18)=Number(entry.p,L"y2");
                Field<int>(fh,0x4C)=Number(entry.p,L"prev"); Field<int>(fh,0x50)=Number(entry.p,L"next");
                Field<double>(fh,0x30)=1; Field<double>(fh,0x38)=0;
                Field<double>(fh,0x40)=Field<int>(fh,0x14)-Field<int>(fh,0x0C);
                void* ref[2]={nullptr,fh};
                InsertHook(space+0x44,nullptr,Field<int>(fh,0x0C),Field<int>(fh,0x10),
                    Field<int>(fh,0x14),Field<int>(fh,0x18),ref);
            }
        }
    }
    for(auto& p:state.platforms) {
        phase="build native platform layer";
        // Real map layers are relative to a world-position origin vector.
        // A bare vector hid the double-position bug in the original test.
        auto origin=New(factory,L"Shape2D#Vector2D",VectorIID());
        Check(Method<HRESULT(__stdcall*)(void*,int,int)>(origin.p,0x40)(origin.p,p.initialX,p.initialY));
        Object layer;
        VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
        phase="CreateLayer from platform artwork";
        Check(Method<HRESULT(__stdcall*)(void*,int,int,unsigned,unsigned,int,VARIANT,VARIANT,void**)>(graphics,0x64)
            (graphics,0,0,0,0,9,Integer(0),missing,&layer.p));
        phase="insert original platform canvas";
        Value inserted;
        Check(Method<HRESULT(__stdcall*)(void*,void*,VARIANT,VARIANT,VARIANT,VARIANT,VARIANT,VARIANT*)>(layer.p,0x104)
            (layer.p,platformCanvas,Integer(100),Integer(255),Integer(255),Integer(100),Integer(100),&inserted.v));
        VARIANT parent{};parent.vt=VT_UNKNOWN;parent.punkVal=static_cast<IUnknown*>(origin.p);
        phase="assign platform origin";
        Check(Method<HRESULT(__stdcall*)(void*,VARIANT)>(layer.p,0x64)(layer.p,parent));
        Require(Int(layer.p,0x20)==p.initialX-Int(platformCanvas,0x6C) &&
            Int(layer.p,0x28)==p.initialY-Int(platformCanvas,0x74),"native world origin and canvas origin both retained");
        phase="set layer visibility";
        Check(Method<HRESULT(__stdcall*)(void*,int)>(layer.p,0x11C)(layer.p,1));
        phase="hide native named layer alpha";
        auto alpha=LayerAlpha(layer.p);
        Check(Method<HRESULT(__stdcall*)(void*,int,int)>(alpha.p,0x40)(alpha.p,0,0));
        auto& record=records[p.name];
        record[0]=0; record[1]=reinterpret_cast<unsigned>(record.data()+3);
        record[2]=1; record[5]=reinterpret_cast<unsigned>(layer.p);
        layers.push_back(std::move(layer));
        origins.push_back(std::move(origin));
    }
}
double __cdecl ReadDouble(const void* p,unsigned) {return *static_cast<const double*>(p);}
unsigned __fastcall StoreDouble(void* p,void*,double value) {*static_cast<double*>(p)=value;return 0x1234;}
static void* queryResult[2]{};
int __fastcall NotFound(void*,void*,void*,void*) { return 0; }
void* __fastcall AddResult(void*,void*) { return queryResult; }
void* __fastcall CopyResult(void* out,void*,void* in) { Field<void*>(out,4)=Field<void*>(in,4);return out; }
bool NativeQuery(const RECT& envelope,void* fh,int x,int y) {
    unsigned char node[0x98]{},record[0x20]{};
    std::memcpy(node+8,&envelope,sizeof(envelope));
    Field<void*>(node,0x1C)=record;Field<int>(node,0x94)=1;Field<void*>(record,0x14)=fh;
    RECT query{x,y,x,y};queryResult[1]=nullptr;
    using Query=void(__thiscall*)(void*,void*,const RECT*,void*);
    reinterpret_cast<Query>(Native(0x0050892D))(space+0x44,node,&query,queryResult);
    return queryResult[1]==fh;
}
void* Resource(void* rm,const wchar_t* name,Value& out) {
    VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    BSTR path=SysAllocString(name);
    Check(Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT,VARIANT,VARIANT*)>(rm,0x1C)(rm,path,missing,missing,&out.v));
    SysFreeString(path);return Unknown(out.v);
}
}
int Run(int argc,char** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    Require(argc==3,"EXE and client directory");
    auto image=TestSupport::LoadImage(argv[1]);
    Require(!VisitorsLabPlatforms::Install(nullptr),"null install");
    auto* bad=reinterpret_cast<unsigned char*>(Native(0x00531B8D));
    *bad^=1;Require(!VisitorsLabPlatforms::Install(image)&&hooks==0,"version mismatch makes no writes");*bad^=1;
    failHook=2;Require(!VisitorsLabPlatforms::Install(image)&&hooks==0,"partial hook rollback");
    failHook=-1;Require(VisitorsLabPlatforms::Install(image)&&hooks==4,"four verified hooks");
    Require(VisitorsLabPlatforms::Install(image)&&hooks==4,"idempotent install");
    TestSupport::Jump(Native(0x0064527E),Find);
    TestSupport::Jump(Native(0x00539338),ReadDouble);
    TestSupport::Jump(Native(0x005393B6),StoreDouble);
    TestSupport::Jump(Native(0x00509149),NotFound);
    TestSupport::Jump(Native(0x005090D1),AddResult);
    TestSupport::Jump(Native(0x00508A2E),CopyResult);
    nativeLoad=reinterpret_cast<Load>(LoadStub);nativeClear=reinterpret_cast<Call>(ClearStub);
    nativeUpdate=reinterpret_cast<Call>(UpdateStub);
    void* fieldVtable[]={reinterpret_cast<void*>(Native(0x00531B8D))};
    Field<void*>(field,0)=fieldVtable;
    unsigned char app[0x20]{};
    // The isolated PE loader copies code without rebasing absolute operands.
    // Keep the actual native clock getter, rebasing only its app pointer slot.
    TestSupport::Bytes(0x00987257,"\xA1\x38\x7B\xBE\x00\x8B\x40\x18\xC3",9);
    *reinterpret_cast<DWORD*>(Native(0x00987258))=Native(0x00BE7B38);
    *reinterpret_cast<void**>(Native(0x00BE7B38))=app;
    nativeInsert=reinterpret_cast<Insert>(InsertStub);
    *reinterpret_cast<void**>(Native(0x00BEBFA0))=space;
    Require(SetCurrentDirectoryA(argv[2]),"client PCOM directory");
    Check(CoInitialize(nullptr));
    auto pc=LoadLibraryW(L"PCOM.dll");Require(pc,"PCOM load");
    Check(reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(pc,"PcInitModule"))());
    std::atexit([]{reinterpret_cast<void(__cdecl*)()>(GetProcAddress(GetModuleHandleW(L"PCOM.dll"),"PcTermModule"))();});
    factory=reinterpret_cast<Factory>(GetProcAddress(pc,"PcCreateObject"));
    const GUID rmId={0x57dfe40b,0x3e20,0x4dbc,{0x97,0xe8,0x80,0x5a,0x50,0xf3,0x81,0xbf}};
    const GUID nsId={0x2aeeeb36,0xa4e1,0x4e2b,{0x8f,0x6f,0x2e,0x7b,0xde,0xc5,0xc5,0x3d}};
    const GUID fsId={0x352d8655,0x51e4,0x4668,{0x8c,0xe4,0x08,0x66,0xe2,0xb6,0xa5,0xb5}};
    auto rm=New(factory,L"ResMan",rmId),ns=New(factory,L"NameSpace",nsId),fs=New(factory,L"NameSpace#FileSystem",fsId);
    Check(Method<HRESULT(__stdcall*)(void*,int,int,int)>(rm.p,0x14)(rm.p,0x11,-1,-1));
    Check(reinterpret_cast<HRESULT(__cdecl*)(void**,int)>(GetProcAddress(pc,"PcRootNameSpace"))(&ns.p,1));
    BSTR path=SysAllocString(L"Data");Check(Method<HRESULT(__stdcall*)(void*,BSTR)>(fs.p,0x34)(fs.p,path));SysFreeString(path);
    path=SysAllocString(L"/");Check(Method<HRESULT(__stdcall*)(void*,BSTR,void*,int)>(ns.p,0x18)(ns.p,path,fs.p,0));SysFreeString(path);
    Value mapValue;Resource(rm.p,L"Map/Map/Map5/502023000.img",mapValue);
    auto root=Query(mapValue.v,PropertyIID());Require(root.p,"actual installed laboratory IMG");
    const GUID grId={0xe576ea33,0xd465,0x4f08,{0xaa,0xb1,0xe7,0x8d,0xf7,0x3e,0xe6,0xd9}};
    auto gr=New(factory,L"Gr2D_DX8",grId);graphics=gr.p;
    phase="initialize graphics";
    auto window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Lab platform render test",
        WS_POPUP,-32000,-32000,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(window,"own hidden render window");
    VARIANT hwnd=Integer(int(window));hwnd.vt|=VT_BYREF;
    Check(Method<HRESULT(__stdcall*)(void*,unsigned,unsigned,VARIANT,VARIANT,VARIANT)>(graphics,0xC)
        (graphics,800,600,hwnd,Integer(0),Integer(0)));
    Object camera;Check(Method<HRESULT(__stdcall*)(void*,void**)>(graphics,0x5C)(graphics,&camera.p));
    phase="position render camera";
    Check(Method<HRESULT(__stdcall*)(void*,int,int)>(camera.p,0x40)(camera.p,-197,660));
    Check(Method<HRESULT(__stdcall*)(void*,unsigned)>(graphics,0x50)(graphics,0xFF000000));
    for(int offset:{0x54,0x58}) {
        phase="set graphics tone";
        Object tone;Check(Method<HRESULT(__stdcall*)(void*,void**)>(graphics,offset)(graphics,&tone.p));
        VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
        Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(tone.p,0x90)(tone.p,255,255,missing,missing));
    }
    auto** deviceTable=Field<void**>(Field<void*>(graphics,0x90),0);
    auto originalPresent=deviceTable[15];ReplaceSlot(&deviceTable[15],reinterpret_cast<void*>(NoPresent));
    Value spriteValue;Resource(rm.p,L"Map/Obj/visitor.img/Henesis_future_energy/move/0/0",spriteValue);
    auto sprite=Query(spriteValue.v,CanvasIID());Require(sprite.p,"original installed platform artwork");
    platformCanvas=sprite.p;
    Field<void*>(field,0x30)=root.p;
    LoadHook(field,nullptr,502023000);
    phase="verify platform binding";
    Require(state.ready && state.platforms.size()==9 && state.serials.size()==9,"all original platforms bound");
    for(auto& p:state.platforms) {
        auto alpha=LayerAlpha(p.layer);
        Require(Int(alpha.p,0x20)==255,"all nine original named platform layers are visible");
    }
    const auto visiblePixels=RenderedPixels();
    Require(visiblePixels>500,"installed platform artwork actually renders on its original route");
    for(auto& p:state.platforms) {
        auto alpha=LayerAlpha(p.layer);
        Check(Method<HRESULT(__stdcall*)(void*,int,int)>(alpha.p,0x40)(alpha.p,0,0));
    }
    Require(RenderedPixels()==0,"transparent v83 named layers reproduce missing platforms");
    BindLayers(field);
    Require(RenderedPixels()==visiblePixels,"platform activation restores the rendered artwork");
    Require(feet.size()==26 && envelopes.size()==26,"every original foothold indexed once");
    Require(envelopes[18].top==636 && envelopes[18].bottom==1301,"vertical sweep covers entire original route");
    Require(envelopes[23].left==565 && envelopes[23].right==989,"horizontal sweep covers entire original route");
    using Project=void(__thiscall*)(void*,void*,void*);
    auto project=reinterpret_cast<Project>(Native(0x009B1646));
    unsigned minY=0xffffffff,maxY=0;
    std::map<int,RECT> original;
    for(auto& entry:feet) {auto* f=entry.second.data;original[entry.first]={Field<int>(f,0x0C),Field<int>(f,0x10),Field<int>(f,0x14),Field<int>(f,0x18)};}
    // Ten-minute full run: real WZ vectors and native standing projection.
    // Standing offsets and velocity stay intact; translation is not teleport.
    for(unsigned time=1000;time<=601000;time+=30) {
        Field<unsigned>(app,0x18)=time;
        UpdateHook(field,nullptr);Require(state.ready,"graphics movement continues through native update entry and clock");
        for(auto& p:state.platforms) {
            Require(Int(p.layer,0x20)==p.motion.x-Int(platformCanvas,0x6C) &&
                Int(p.layer,0x28)==p.motion.y-Int(platformCanvas,0x74),"native rendered image follows motion with original canvas offset");
            auto alpha=LayerAlpha(p.layer);Require(Int(alpha.p,0x20)==255,"platform stays visible throughout motion");
            for(auto* fh:p.footholds) {
                int sn=Field<int>(fh,0x48);const auto before=original[sn];const auto box=envelopes[sn];
                Require(Field<int>(fh,0x0C)==before.left+p.motion.x-p.initialX &&
                    Field<int>(fh,0x10)==before.top+p.motion.y-p.initialY,"visual and collision translation agree");
                Require(box.left<=Field<int>(fh,0x0C)&&Field<int>(fh,0x14)<=box.right &&
                    box.top<=Field<int>(fh,0x10)&&Field<int>(fh,0x18)<=box.bottom,"current contact surface stays in broad phase");
                Require(NativeQuery(box,fh,Field<int>(fh,0x0C)+30,Field<int>(fh,0x10)),"native spatial query finds lift at every moved position");
                Require(!NativeQuery(box,fh,box.right+1,box.bottom+1),"native spatial query rejects points outside travel envelope");
                alignas(8) unsigned char relative[0x30]{},absolute[0x60]{};
                Field<double>(relative,0)=30.25;Field<double>(relative,0x18)=12.5;
                project(absolute,relative,fh);
                Require(Field<double>(absolute,0)==Field<int>(fh,0x0C)+30.25 &&
                    Field<double>(absolute,0x18)==Field<int>(fh,0x10),"native rider position follows both axes");
                Require(Field<double>(relative,0)==30.25 && Field<double>(absolute,0x30)==12.5,"native walking velocity preserved");
                if(sn==18){minY=(std::min)(minY,static_cast<unsigned>(p.motion.y));maxY=(std::max)(maxY,static_cast<unsigned>(p.motion.y));}
            }
        }
    }
    Require(minY==660 && maxY==1325,"left lift visits both original endpoints");
    Require(updates==20001,"every hooked update forwards the original field update");
    Require(diagnosticLines.size()==3,"one load, first update and movement snapshot per visit; no per-frame log");
    Require(diagnosticLines[1].find("first-update")!=std::string::npos &&
        diagnosticLines[2].find("motion-check")!=std::string::npos,"actual update and movement evidence recorded");
    unsigned char previousField[0x200]{};
    ClearHook(previousField,nullptr);
    Require(state.ready && clears==1,"leaving the previous field does not clear the newly loaded lab");
    ClearHook(field,nullptr);Require(!state.ready && state.platforms.empty() && clears==2,"exit clears every borrowed native pointer");
    Tick(field,700000);LoadHook(field,nullptr,100000000);Require(!state.ready,"other maps stay native");
    LoadHook(field,nullptr,502023000);Require(state.ready && state.platforms[0].motion.y==660,"reentry resets original route and timer");
    auto count=state.platforms.size();
    *reinterpret_cast<void**>(Native(0x00BEBFA0))=nullptr;Tick(field,800000);
    Require(state.platforms.size()==count && !state.platforms[0].motion.started,"stale physical space is never touched");
    Motion wrap;wrap.speed=10;wrap.x1=0;wrap.x2=100;wrap.started=true;wrap.last=0xfffffff0;
    Require(wrap.Step(0x22) && wrap.x==5,"32-bit clock wrap");
    Motion slow;slow.speed=35;slow.y1=1186;slow.y2=1379;slow.y=1186;
    slow.Step(100);Require(!slow.Step(130),"original slow platform waits for sufficient elapsed time");
    slow.Step(160);slow.Step(220);Require(slow.y==1187,"original endpoint direction reversal and 35ms/pixel rate");
    state={};layers.clear();origins.clear();
    ReplaceSlot(&deviceTable[15],originalPresent);
    DreamCanvas::Release(camera.detach());
    Check(Method<HRESULT(__stdcall*)(void*)>(graphics,0x10)(graphics));
    DreamCanvas::Release(gr.detach());DestroyWindow(window);
    std::printf("PASS installed IMG: %u visible platform pixels, transparent negative control, 9 native Gr2D layers with world origins, 26 footholds, 10-minute routes, native rider projection, exit/reentry, hook guards/rollback\n",visiblePixels);
    return 0;
}
int main(int argc,char** argv) {
    try { return Run(argc,argv); }
    catch(HRESULT hr) { std::fprintf(stderr,"FAIL %s HRESULT=%08lX\n",phase,hr);std::_Exit(1); }
    catch(...) { std::fprintf(stderr,"FAIL %s unexpected exception\n",phase);std::_Exit(1); }
}
