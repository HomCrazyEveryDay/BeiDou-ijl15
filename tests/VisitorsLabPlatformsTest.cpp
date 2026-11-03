#define NOMINMAX
#define VISITORS_LAB_TEST
#include "../ezorsia/VisitorsLabPlatforms.cpp"
#include "VisitorsShuttleTest.h"
#include <array>
#include <cstdio>
#include <cstdlib>

using TestSupport::Require;
namespace ClientLog { void Append(Component,const char*,...) {} }
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
std::vector<Object> layers;
Factory factory;
int clears = 0, loads = 0;
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
    feet.clear(); records.clear(); layers.clear(); envelopes.clear();
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
        auto layer=New(factory,L"Shape2D#Vector2D",VectorIID());
        Check(Method<HRESULT(__stdcall*)(void*,int,int)>(layer.p,0x40)(layer.p,p.initialX,p.initialY));
        auto& record=records[p.name];
        record[0]=0; record[1]=reinterpret_cast<unsigned>(record.data()+3);
        record[2]=1; record[5]=reinterpret_cast<unsigned>(layer.p);
        layers.push_back(std::move(layer));
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
int main(int argc,char** argv) {
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
    Field<void*>(field,0x30)=root.p;
    LoadHook(field,nullptr,502023000);
    Require(state.ready && state.platforms.size()==9 && state.serials.size()==9,"all original platforms bound");
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
        Tick(field,time);Require(state.ready,"graphics movement continues");
        for(auto& p:state.platforms) {
            Require(Int(p.layer,0x20)==p.motion.x && Int(p.layer,0x28)==p.motion.y,"actual PCOM image vector follows resource position");
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
    ClearHook(field,nullptr);Require(!state.ready && state.platforms.empty() && clears==1,"exit clears every borrowed native pointer");
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
    state={};layers.clear();
    std::puts("PASS installed IMG: 9 moving platforms, 26 footholds, 10-minute routes, real PCOM vectors, native rider projection, exit/reentry, hook guards/rollback");
}
