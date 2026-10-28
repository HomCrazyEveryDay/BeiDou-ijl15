#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "Memory.h"
namespace CrashReporter {void RecordEvent(const char*,const char*,...) {}}
bool Memory::SetHook(bool,void**,void*){return false;}
#include "ModernWeaponResources.h"
#include "ModernCapeEffects.h"
#include "../ezorsia/ChairCompatibility.cpp"
namespace ClientLog {void Append(Component,const char*,...) {}}
using namespace MixedDyeResources;
void Require(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL %s\n",text);std::exit(1);}}
struct Layer {void** table;int refs=1,flip=0,z=0,alpha=-1,visible=1,frames=0,animations=0;void* origin=nullptr;void* overlay=nullptr;std::vector<int> delays;int x=0,y=0;};
static void* layerMethods[80]{};static void* graphMethods[32]{};static int creates=0,live=0;
ULONG __stdcall Add(Layer* p){return ++p->refs;}
ULONG __stdcall Release(Layer* p){if(--p->refs)return p->refs;--live;delete p;return 0;}
HRESULT __stdcall Origin(Layer* p,VARIANT v){p->origin=MixedDyeResources::Unknown(v);return S_OK;}
HRESULT __stdcall GetOrigin(Layer* p,VARIANT* v){*v=ModernCapeEffects::Borrow(p->origin);if(v->punkVal)v->punkVal->AddRef();return S_OK;}
HRESULT __stdcall GetX(Layer* p,LONG* v){*v=p->x;return S_OK;}
HRESULT __stdcall GetY(Layer* p,LONG* v){*v=p->y;return S_OK;}
HRESULT __stdcall GetFlip(Layer* p,LONG* v){*v=p->flip;return S_OK;}
HRESULT __stdcall Move(Layer* p,int x,int y,VARIANT,VARIANT){p->x=x;p->y=y;return S_OK;}
HRESULT __stdcall Overlay(Layer* p,VARIANT v){p->overlay=MixedDyeResources::Unknown(v);return S_OK;}
HRESULT __stdcall Flip(Layer* p,int v){p->flip=v;return S_OK;}
HRESULT __stdcall Alpha(Layer* p,unsigned v){p->alpha=v;return S_OK;}
HRESULT __stdcall Visible(Layer* p,int v){p->visible=v;return S_OK;}
HRESULT __stdcall GetAlpha(Layer* p,LONG* v){*v=p->alpha;return S_OK;}
HRESULT __stdcall GetVisible(Layer* p,LONG* v){*v=p->visible;return S_OK;}
HRESULT __stdcall Insert(Layer* p,void* canvas,VARIANT delay,VARIANT a0,VARIANT a1,VARIANT z0,VARIANT z1,VARIANT* result){
    // Ghost eye effects intentionally use a transparent 1x1 climbing frame.
    Require(canvas && Int(canvas,0x40)>0 && Int(canvas,0x48)>0,"real effect canvas passed to native layer ABI");
    Require(delay.vt==VT_I4 && delay.lVal>0 && a0.vt==VT_I4 && a1.vt==VT_I4 && z0.vt==VT_I4 && z1.vt==VT_I4,"native frame variant ABI");
    ++p->frames;p->delays.push_back(delay.lVal);VariantInit(result);return S_OK;
}
HRESULT __stdcall Animate(Layer* p,int type,VARIANT,VARIANT){Require(type==0x20,"continuous animation");++p->animations;return S_OK;}
HRESULT __stdcall Create(void*,int x,int y,unsigned w,unsigned h,int z,VARIANT,VARIANT,void** out){
    Require(x==0 && y==0 && w==0 && h==0,"native origin, no baked position or size");
    auto p=new Layer{layerMethods};p->z=z;*out=p;++creates;++live;return S_OK;
}
void InitMock(){
    layerMethods[1]=reinterpret_cast<void*>(Add);layerMethods[2]=reinterpret_cast<void*>(Release);
    layerMethods[0x64/4]=reinterpret_cast<void*>(Origin);layerMethods[0xfc/4]=reinterpret_cast<void*>(Overlay);
    layerMethods[0x60/4]=reinterpret_cast<void*>(GetOrigin);layerMethods[0x20/4]=reinterpret_cast<void*>(GetX);layerMethods[0x28/4]=reinterpret_cast<void*>(GetY);
    layerMethods[0xd4/4]=reinterpret_cast<void*>(GetFlip);layerMethods[0x90/4]=reinterpret_cast<void*>(Move);
    layerMethods[0xd8/4]=reinterpret_cast<void*>(Flip);layerMethods[0xe0/4]=reinterpret_cast<void*>(Alpha);layerMethods[0xdc/4]=reinterpret_cast<void*>(GetAlpha);
    layerMethods[0x11c/4]=reinterpret_cast<void*>(Visible);layerMethods[0x118/4]=reinterpret_cast<void*>(GetVisible);
    layerMethods[0x104/4]=reinterpret_cast<void*>(Insert);layerMethods[0x110/4]=reinterpret_cast<void*>(Animate);
    graphMethods[0x64/4]=reinterpret_cast<void*>(Create);
}
void CheckWeapons(Factory factory,void* manager){
    int adapted=0;
    for(int id:{1702713,1702804,1703139,1703238,1703243,1703373,1703419,1703425,1703451,1703471,1703552,1703759,1703762}) {
        wchar_t path[80];swprintf_s(path,L"Character/Weapon/%08d.img",id);
        auto value=ModernCapeEffects::Resource(path,manager);auto original=Query(value.v,PropertyIID());Require(original.p,"weapon exists");
        auto names=Names(original.p);auto replacement=ModernWeaponResources::Adapt(original.p,factory);
        if(id==1702713 || id==1702804){Require(!replacement.p,"complete legacy roots preserved");continue;}
        if(!replacement.p)std::printf("unadapted=%d\n",id);
        Require(replacement.p,"modern universal schema recognized");++adapted;
        VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
        auto native=Method<ModernWeaponResources::Getter>(manager,0x1c);
        BSTR request=SysAllocString(path);Value hooked;
        Check(ModernWeaponResources::GetObject(native,factory,manager,request,missing,missing,&hooked.v));SysFreeString(request);
        auto hookedRoot=Query(hooked.v,PropertyIID());Require(hookedRoot.p,"production resource interception");
        auto bow=Resolve(hookedRoot.p,L"45/shoot1");Require(Query(bow.v,PropertyIID()).p,"native caller reads common bow actions from intercepted root");
        std::wstring suffix=std::wstring(path)+L"/45/shoot1";request=SysAllocString(suffix.c_str());Value direct;
        Check(ModernWeaponResources::GetObject(native,factory,manager,request,missing,missing,&direct.v));SysFreeString(request);
        Require(Query(direct.v,PropertyIID()).p,"direct action lookup uses the same schema");
        for(auto type:{L"30",L"31",L"32",L"33",L"37",L"38",L"40",L"41",L"42",L"43",L"44",L"45",L"46",L"47",L"48",L"49"}) {
            auto actual=Resolve(replacement.p,type);auto root=Query(actual.v,PropertyIID());Require(root.p,"all legacy types readable");
            for(const auto& action:Names(root.p)) {
                auto frame=Resolve(replacement.p,std::wstring(type)+L"/"+action);Require(!Empty(frame.v),"all action UOLs resolve");
            }
        }
        Require(names==Names(original.p),"source root not mutated");
        auto a=Resolve(original.p,L"49"),b=Resolve(replacement.p,L"49");Require(MixedDyeResources::Unknown(a.v)==MixedDyeResources::Unknown(b.v),"original gun artwork retained");
    }
    Require(adapted==11,"eleven universal weapons");
    auto restricted=New(factory,L"Property",PropertyIID());auto common=New(factory,L"Property",PropertyIID());PutObject(restricted.p,L"30",common.p);
    Require(!ModernWeaponResources::Adapt(restricted.p,factory).p,"single-type restricted weapon is not expanded");
    std::puts("PASS 13 weapons: 11 adapted, two native, restricted/source/gun preserved");
}
void CheckCapes(Factory factory,void* manager){
    InitMock();void** graph=graphMethods;Layer overlay{layerMethods};
    auto body=New(factory,L"Shape2D#Vector2D",VectorIID()),head=New(factory,L"Shape2D#Vector2D",VectorIID());
    BYTE avatar[0x1100]{};int foot=0;
    *reinterpret_cast<void**>(avatar+0x10b0)=&foot;
    *reinterpret_cast<void**>(avatar+0x10b4)=head.p;
    *reinterpret_cast<void**>(avatar+0x10b8)=body.p;
    auto move=[](void* vector,int x,int y){
        VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
        Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(vector,0x90)(vector,x,y,missing,missing));
    };
    for(int id:{1103946,1103654,1103649,1103552,1103949,1103483}) {
        std::printf("cape=%d\n",id);
        auto effect=ModernCapeEffects::EffectFor(id,manager);if(!effect.p)std::printf("missingcape=%d\n",id);Require(effect.p,"modern cape and dependency detected");
        ModernCapeEffects::State state;state.id=id;state.effect.p=effect.detach();
        for(const auto* action:{L"stand1",L"walk1",L"walk2",L"prone",L"proneStab",L"jump",L"rope",L"ladder",L"swingO1"}) {
            const std::wstring name=action;const bool prone=name==L"prone" || name==L"proneStab";
            // Expectations from the CMS source actions, independent of the
            // production selector: mixed-pos capes must switch both ways.
            const bool atHead=id==1103654 || id==1103649 || (id==1103483 && !prone) ||
                (id==1103949 && (prone || name==L"jump" || name==L"rope" || name==L"ladder"));
            void* expected=atHead?head.p:body.p;
            move(body.p,9,-24);move(head.p,prone?-7:-3,prone?-28:-52);
            ModernCapeEffects::Sync(state,&graph,avatar,&overlay,action,0);
            auto layer=static_cast<Layer*>(state.layer.p);Require(layer && layer->frames && layer->animations==1,"full independent frame sequence");
            Require(layer->origin==expected && state.origin==expected,"source action selects native head/body anchor, never foot");
            auto sequence=ModernCapeEffects::Sequence(state.effect.p,action);auto frames=Resolve(state.effect.p,sequence);auto property=Query(frames.v,PropertyIID());
            for(int i=0;i<layer->frames;++i){auto frame=Resolve(state.effect.p,sequence+L"/"+std::to_wstring(i));auto canvas=Query(frame.v,CanvasIID());auto meta=CanvasProperty(canvas.p);Require(layer->delays[i]==ModernCapeEffects::Number(meta.p,L"delay",100),"source animation delays unchanged");}
            int same=creates;move(expected,11,-37);
            ModernCapeEffects::Sync(state,&graph,avatar,&overlay,action,1);Require(creates==same && layer->flip==1,"direction change preserves animation time");
            Require(Int(layer->origin,0x20)==11 && Int(layer->origin,0x28)==-37,"native frame/facing/seat movement follows live vector without extra offset or mirror");
            overlay.alpha=0x77ffffff;overlay.visible=0;ModernCapeEffects::Sync(state,&graph,avatar,&overlay,action,1);
            layer=static_cast<Layer*>(state.layer.p);
            Require(layer->alpha==overlay.alpha && layer->visible==0,"hiding/alpha follows avatar");overlay.alpha=-1;overlay.visible=1;
        }
        // Rebuild/unequip transitions can temporarily remove the selected vector.
        void** slot=reinterpret_cast<void**>(avatar+(id==1103654 || id==1103649 || id==1103483?0x10b4:0x10b8));
        void* saved=*slot;*slot=nullptr;ModernCapeEffects::Sync(state,&graph,avatar,&overlay,L"stand1",0);
        Require(!state.layer.p && !state.failed,"missing vector clears stale effect without permanent failure");
        *slot=saved;ModernCapeEffects::Sync(state,&graph,avatar,&overlay,L"stand1",0);
        Require(state.layer.p && static_cast<Layer*>(state.layer.p)->origin==saved,"effect recovers once avatar vector is ready");
        // Replacing the head/body object must release the old layer and bind the new one.
        auto replacement=New(factory,L"Shape2D#Vector2D",VectorIID());*slot=replacement.p;
        int before=creates;ModernCapeEffects::Sync(state,&graph,avatar,&overlay,L"stand1",0);
        Require(creates==before+1 && static_cast<Layer*>(state.layer.p)->origin==replacement.p && live==1,"anchor object replacement rebuilds layer without leak");
        *slot=saved;ModernCapeEffects::Sync(state,&graph,avatar,&overlay,L"stand1",0);
        ModernCapeEffects::Sync(state,&graph,avatar,nullptr,L"stand1",0);Require(!state.layer.p,"missing avatar overlay clears effect");
    }
    Require(live==0,"effect layers released after lifecycle");
    Require(!ModernCapeEffects::EffectFor(1102958,manager).p,"ordinary cape stays native");
    std::puts("PASS six capes: 54 action anchors, live native vectors, original frames/delays, flip, visibility, rebuild recovery and layer ownership");
}
void CheckEquipmentEffects(Factory factory,void* manager){
    using namespace ModernCapeEffects;
    void** graph=graphMethods;Layer overlay{layerMethods},face{layerMethods};
    auto body=New(factory,L"Shape2D#Vector2D",VectorIID()),head=New(factory,L"Shape2D#Vector2D",VectorIID());
    BYTE avatar[0x1100]{};
    *reinterpret_cast<void**>(avatar+0x10b4)=head.p;*reinterpret_cast<void**>(avatar+0x10b8)=body.p;
    *reinterpret_cast<void**>(avatar+0x10c0)=&face;*reinterpret_cast<void**>(avatar+0x10c8)=&overlay;
    int actions=0;
    for(int id:{1053910,1053909,1053647,1053646,1073379,1022242,1022241,1006882,1006883,1074003}) {
        auto effect=EffectFor(id,manager);Require(effect.p,"all ten missing ItemEff dependencies load");
        State state;state.id=id;state.effect.p=effect.detach();
        for(const auto& name:Names(state.effect.p)) {
            auto value=Resolve(state.effect.p,name);auto frames=Query(value.v,PropertyIID());
            if(!frames.p)continue;
            auto first=Resolve(state.effect.p,name+L"/0");if(!Query(first.v,CanvasIID()).p)continue;
            Sync(state,&graph,avatar,&overlay,name,0);
            auto layer=static_cast<Layer*>(state.layer.p);
            Require(layer && layer->frames && state.sequence==name,"every declared action resolves without silently falling back");
            Require(layer->origin==(Number(frames.p,L"pos",0)==1?head.p:body.p),"original action anchor preserved");
            for(int i=0;i<layer->frames;++i) {
                auto frame=Resolve(state.effect.p,name+L"/"+std::to_wstring(i));auto canvas=Query(frame.v,CanvasIID());auto meta=CanvasProperty(canvas.p);
                Require(layer->delays[i]==Number(meta.p,L"delay",100),"all action/frame UOLs and delays preserved");
            }
            int before=creates;Sync(state,&graph,avatar,&overlay,name,1);
            Require(creates==before && layer->flip==1,"turning does not restart item animations");
            ++actions;
        }
        Sync(state,&graph,avatar,&overlay,L"stand1",0);
        auto layer=static_cast<Layer*>(state.layer.p);
        if(id==1022241 || id==1022242)Require(layer->overlay==&face && layer->z==1 && layer->frames==6,"eye effect follows head and face depth");
        if(id==1073379) {
            Require(layer->frames==8 && layer->z==-1 && state.sequence==L"stand1","fixed=1 shoe keeps its eight frames and stand depth");
            Sync(state,&graph,avatar,&overlay,L"ladder",0);layer=static_cast<Layer*>(state.layer.p);
            Require(layer->frames==8 && layer->z==2 && state.sequence==L"ladder","shoe ladder frame UOL selects back artwork and front depth");
        }
        if(id==1074003){Sync(state,&graph,avatar,&overlay,L"walk2",0);Require(static_cast<Layer*>(state.layer.p)->frames==4 && state.sequence==L"walk2","water shoe action UOL plays walk1 frames");}
    }
    Require(live==0,"ten independent item effect layers release");
    const int slots[]={1,3,5,7,9},ids[]={1006882,1022241,1053910,1073379,1103649};
    for(int i=0;i<5;++i)std::memcpy(avatar+0x1d+slots[i]*4,&ids[i],4);
    TickEquipment(avatar,manager,&graph,L"stand1",0);Require(live==5,"hat, eyes, coat, shoes and cape coexist");
    auto& state=States().at(avatar);for(int i=0;i<5;++i)Require(state[slots[i]].id==ids[i] && state[slots[i]].layer.p,"visible equip slots map to the correct item ID");
    int before=creates;TickEquipment(avatar,manager,&graph,L"stand1",1);Require(creates==before,"unchanged equip set preserves all animation clocks");
    HiddenByChair()=[](void*,int id){return id/10000==110;};TickEquipment(avatar,manager,&graph,L"stand1",0);
    for(int slot:slots)Require(static_cast<Layer*>(state[slot].layer.p)->visible==(slot==9?0:1),"invisibleCape hides only the cape");
    HiddenByChair()=[](void*,int){return true;};TickEquipment(avatar,manager,&graph,L"stand1",0);
    for(int slot:slots)Require(static_cast<Layer*>(state[slot].layer.p)->visible==0,"removeEffect hides all modern equipment layers");
    HiddenByChair()=nullptr;TickEquipment(avatar,manager,&graph,L"stand1",0);
    for(int slot:slots)Require(static_cast<Layer*>(state[slot].layer.p)->visible==1,"standing restores effect visibility");
    int empty=0;std::memcpy(avatar+0x39,&empty,4);TickEquipment(avatar,manager,&graph,L"stand1",0);
    Require(live==4 && !state[7].layer.p && state[9].layer.p,"unequipping shoes releases only their layer");
    States().erase(avatar);Require(live==0,"avatar destruction releases all equipment effects");
    std::printf("PASS ten additional effects: %d source actions, nested UOLs, five concurrent slots, visibility and lifecycle\n",actions);
}
void CheckChairCoordinates(void* manager){
    for(auto entry:{POINT{3018197,-72},POINT{3019686,-123},POINT{3018806,-153}}){
        wchar_t path[100];swprintf_s(path,L"Item/Install/0301.img/%08d/info/bodyRelMove",entry.x);
        auto value=ModernCapeEffects::Resource(path,manager);auto vector=Query(value.v,VectorIID());
        Require(vector.p && Int(vector.p,0x28)==entry.y,"three chair positions readable through real v83 PCOM");
    }
    std::puts("PASS three chairs: native vector readback preserves source body positions");
}
static int petUpdates=0,petPrepares=0,petDestructions=0,chairFlip=0;
static void* expectedPetOrigin=nullptr;
void __fastcall NativePetUpdate(void* pet,void*){
    auto layer=*reinterpret_cast<Layer**>(static_cast<BYTE*>(pet)+0x124);
    Require(layer->origin==expectedPetOrigin && layer->x==7 && layer->y==8,"normal pet origin restored before native AI/frame update");++petUpdates;
}
void __fastcall NativePetPrepare(void*,void*){++petPrepares;}
void __fastcall NativePetDestroy(void*,void*){++petDestructions;}
int __fastcall ChairAction(void*,void*,int* flip){*flip=chairFlip;return 0;}
void CheckChairSettings(Factory factory,void* manager){
    auto pumpkin=ModernChairResources::Read(3019686,manager);
    Require(pumpkin.weapon && pumpkin.cape && pumpkin.effect,"pumpkin source visibility flags");
    auto altar=ModernChairResources::Read(3018806,manager);
    Require(!altar.weapon && !altar.cape && altar.effect,"altar preserves weapon/cape artwork");
    BYTE avatar[0x400]{};int weapon=1702565,cape=1102809;
    std::memcpy(avatar+0x49,&weapon,4);std::memcpy(avatar+0x41,&cape,4);
    auto compose=[](void* p,int){auto b=static_cast<BYTE*>(p);Require(!*reinterpret_cast<int*>(b+0x49) && !*reinterpret_cast<int*>(b+0x41),"native compositor sees hidden slots");};
    ModernChairResources::RefreshLook(avatar,pumpkin,1,compose);
    Require(*reinterpret_cast<int*>(avatar+0x49)==weapon && *reinterpret_cast<int*>(avatar+0x41)==cape && *reinterpret_cast<int*>(avatar+0x1c5+0x49)==weapon,"live/cached equipment restored");
    try{ModernChairResources::RefreshLook(avatar,pumpkin,1,[](void*,int){throw E_FAIL;});}catch(HRESULT){}
    Require(*reinterpret_cast<int*>(avatar+0x49)==weapon,"equipment restored on native exception");
    for(int i=0;i<3;++i){
        auto& seat=altar.seats[i];Require(seat.present && seat.actions.size()==7 && seat.minGap==5000 && seat.maxGap==10000,"all original random pet actions and times");
        Require(ModernChairResources::PetFits(5002414+i,seat,manager),"matching summoned pets use their source grade");
        for(auto& action:seat.actions){wchar_t path[100];swprintf_s(path,L"Item/Pet/%d.img/%s",5002414+i,action.first.c_str());auto value=ModernCapeEffects::Resource(path,manager);Require(Query(value.v,PropertyIID()).p,"pet random action resource exists");}
    }
    Require(!ModernChairResources::PetFits(5000256,altar.seats[0],manager),"unrelated pet grade excluded");
    Require(altar.seats[0].x==23 && altar.seats[0].y==-108 && altar.seats[1].x==-67 && altar.seats[1].reverse==1 && altar.seats[2].x==102,"original three seat positions and reverse facing");
    // Exercise the production pet callback without executing the game. Only
    // native AI/PrepareAction are replaced, PCOM/resource parsing stays real.
    g_chairResourceManager=&manager;
    void* common[9]{};g_petBasicActions=common;
    std::vector<BSTR> actions;std::vector<void*> records;
    for(auto& action:altar.seats[0].actions)actions.push_back(SysAllocString(action.first.c_str()));
    for(auto& action:actions)records.push_back(&action);
    for(size_t i=0;i<records.size();++i)common[i]=records[i];
    g_petUpdate=reinterpret_cast<PetCall>(NativePetUpdate);g_petPrepare=reinterpret_cast<PetCall>(NativePetPrepare);g_petDestroy=reinterpret_cast<PetCall>(NativePetDestroy);
    ModernCapeEffects::actionFor=reinterpret_cast<ModernCapeEffects::ActionCall>(ChairAction);
    auto origin=New(factory,L"Shape2D#Vector2D",VectorIID()),foot=New(factory,L"Shape2D#Vector2D",VectorIID());expectedPetOrigin=origin.p;
    BYTE user[0x3000]{},pet[0x200]{},petTemplate[0x40]{};
    *reinterpret_cast<int*>(petTemplate)=5002415;*reinterpret_cast<void**>(pet+0x88)=petTemplate;
    *reinterpret_cast<void**>(pet+0x94)=user;*reinterpret_cast<int*>(pet+0x9c)=1;*reinterpret_cast<int*>(pet+0xf8)=-1;
    *reinterpret_cast<int*>(user+kActiveChairOffset)=3018806;*reinterpret_cast<void**>(user+0x88+0x10b0)=foot.p;
    Layer layer{layerMethods};layer.origin=origin.p;layer.x=7;layer.y=8;*reinterpret_cast<void**>(pet+0x124)=&layer;
    Layer equipmentEffect{layerMethods},chairEffect{layerMethods};
    *reinterpret_cast<void**>(user+0x2258+0x14)=&equipmentEffect;
    *reinterpret_cast<void**>(user+0x26fc+0x14)=&chairEffect;
    ChairState visibility{3018806,altar};HideItemEffects(user,visibility);
    Require(equipmentEffect.visible==0 && chairEffect.visible==1,"removeEffect hides item effects but preserves chair animation");
    RestoreEffects(visibility);Require(equipmentEffect.visible==1 && equipmentEffect.refs==1,"item effect visibility/reference restored");
    g_chairs.emplace(user+0x88,ChairState{3018806,altar});
    PetUpdateHook(pet,nullptr);Require(layer.origin==foot.p && layer.x==-67 && layer.y==19 && layer.flip==1,"pet seated using source position and reverseFlip");
    chairFlip=1;PetUpdateHook(pet,nullptr);Require(layer.x==67 && layer.flip==0,"seat mirrors with owner direction");
    *reinterpret_cast<int*>(user+kActiveChairOffset)=0;PetUpdateHook(pet,nullptr);
    Require(g_petSeats.empty() && layer.origin==origin.p && layer.x==7 && layer.y==8 && *reinterpret_cast<int*>(pet+0xf8)==-1,"standing restores native pet display/action");
    *reinterpret_cast<int*>(user+kActiveChairOffset)=3018806;PetUpdateHook(pet,nullptr);PetDestroyHook(pet+8,nullptr);
    Require(g_petSeats.empty() && layer.refs==1 && petDestructions==1 && petUpdates==4 && petPrepares>=2,"pet recall lifecycle releases references, native updates preserved");
    g_chairs.clear();for(auto value:actions)SysFreeString(value);
    std::puts("PASS chair flags, equipment restoration, 21 original pet actions, grade filter, facing and pet lifecycle");
}
#include "SchoolSetEffectsCases.h"
#include "ModernStandingActionsCases.h"
#include "SplitChairResourcesCases.h"
int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);setvbuf(stdout,nullptr,_IONBF,0);
    Require((argc==2 || argc==3) && SetCurrentDirectoryW(argv[1]),"client resource directory");Check(CoInitialize(nullptr));
    auto pcom=LoadLibraryW(L"PCOM.dll");Require(pcom,"PCOM");Check(reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(pcom,"PcInitModule"))());
    std::atexit([]{reinterpret_cast<void(__cdecl*)()>(GetProcAddress(GetModuleHandleW(L"PCOM.dll"),"PcTermModule"))();});
    auto factory=reinterpret_cast<Factory>(GetProcAddress(pcom,"PcCreateObject"));
    const GUID rmId={0x57dfe40b,0x3e20,0x4dbc,{0x97,0xe8,0x80,0x5a,0x50,0xf3,0x81,0xbf}};
    const GUID nsId={0x2aeeeb36,0xa4e1,0x4e2b,{0x8f,0x6f,0x2e,0x7b,0xde,0xc5,0xc5,0x3d}};
    const GUID fsId={0x352d8655,0x51e4,0x4668,{0x8c,0xe4,0x08,0x66,0xe2,0xb6,0xa5,0xb5}};
    auto rm=New(factory,L"ResMan",rmId),ns=New(factory,L"NameSpace",nsId),fs=New(factory,L"NameSpace#FileSystem",fsId);
    Check(Method<HRESULT(__stdcall*)(void*,int,int,int)>(rm.p,0x14)(rm.p,0x11,-1,-1));
    Check(reinterpret_cast<HRESULT(__cdecl*)(void**,int)>(GetProcAddress(pcom,"PcRootNameSpace"))(&ns.p,1));
    BSTR path=SysAllocString(argc==3?argv[2]:L"Data");Check(Method<HRESULT(__stdcall*)(void*,BSTR)>(fs.p,0x34)(fs.p,path));SysFreeString(path);
    path=SysAllocString(L"/");Check(Method<HRESULT(__stdcall*)(void*,BSTR,void*,int)>(ns.p,0x18)(ns.p,path,fs.p,0));SysFreeString(path);
    SplitChairTests::Install(factory,rm.p);
    try {SplitChairTests::CheckChairs(rm.p);CheckWeapons(factory,rm.p);CheckCapes(factory,rm.p);CheckEquipmentEffects(factory,rm.p);CheckSchoolSets(factory,rm.p);CheckStandingAndRotation(factory,rm.p);CheckChairCoordinates(rm.p);CheckChairSettings(factory,rm.p);}catch(HRESULT hr){std::printf("HRESULT %08x\n",hr);return 1;}
    return 0;
}
