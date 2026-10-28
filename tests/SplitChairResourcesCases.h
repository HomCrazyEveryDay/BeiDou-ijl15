#include "SplitChairResources.h"

namespace SplitChairTests {
static SplitChairResources::Getter original = nullptr;
static Factory factory = nullptr;
static void* fixtureLegacy=nullptr;
static void* fixtureBucket=nullptr;
inline HRESULT __stdcall FixtureGet(void*,BSTR path,VARIANT,VARIANT,VARIANT* result) {
    VariantInit(result);
    void* object=wcscmp(path,SplitChairResources::LegacyRoot)==0?fixtureLegacy:
        wcscmp(path,L"Item/Install/03018.img")==0?fixtureBucket:nullptr;
    if(object){result->vt=VT_UNKNOWN;result->punkVal=static_cast<IUnknown*>(object);result->punkVal->AddRef();}
    return S_OK;
}
inline void CheckOverrides(Factory create) {
    auto legacy=New(create,L"Property",PropertyIID()),bucket=New(create,L"Property",PropertyIID());
    auto old=New(create,L"Property",PropertyIID()),updated=New(create,L"Property",PropertyIID());
    Put(old.p,L"version",Integer(1));Put(old.p,L"oldOnly",Integer(1));Put(updated.p,L"version",Integer(2));
    Put(legacy.p,L"03018806",ModernCapeEffects::Borrow(old.p));
    Put(legacy.p,L"03018197",ModernCapeEffects::Borrow(old.p));
    Put(bucket.p,L"03018806",ModernCapeEffects::Borrow(updated.p));
    fixtureLegacy=legacy.p;fixtureBucket=bucket.p;
    SplitChairResources::Cache cache;
    SplitChairResources::Build(cache,FixtureGet,create,nullptr,VARIANT{},VARIANT{});
    Require(Names(cache.root.p).size()==2,"duplicate split ID overrides exactly one legacy chair");
    auto version=Resolve(cache.root.p,L"03018806/version"),fallback=Resolve(cache.root.p,L"03018197/version");
    auto stale=Resolve(cache.root.p,L"03018806/oldOnly");
    Require(version.v.lVal==2 && fallback.v.lVal==1 && Empty(stale.v),"override keeps its own fields and missing same-bucket ID preserves legacy");
    Require(cache.locations.size()==1 && cache.locations.at(3018806)==L"Item/Install/03018.img","deep lookup redirects only overridden ID");
    fixtureBucket=nullptr;
    SplitChairResources::Build(cache,FixtureGet,create,nullptr,VARIANT{},VARIANT{});
    Require(cache.ready && !cache.root.p && cache.locations.empty(),"no optional buckets delegates entirely to legacy");
    fixtureLegacy=nullptr;
    std::puts("PASS split chair override: duplicate IDs, missing same-bucket IDs, stale-field isolation and legacy-only resources");
}
inline HRESULT __stdcall GetObject(void* manager,BSTR path,VARIANT a,VARIANT b,VARIANT* result) {
    return SplitChairResources::GetObject(original,factory,manager,path,a,b,result);
}
inline Value Native(void* manager,const wchar_t* path) {
    return SplitChairResources::Load(original,manager,path,VARIANT{},VARIANT{});
}
inline void Install(Factory create,void* manager) {
    factory=create;
    auto slot=&(*reinterpret_cast<void***>(manager))[0x1c/sizeof(void*)];
    original=reinterpret_cast<SplitChairResources::Getter>(*slot);
    // Exercise the production adapter through the same PCOM vtable used by
    // CItemInfo and the existing chair animation/visibility/pet resource readers.
    DWORD protection=0,unused=0;
    Require(VirtualProtect(slot,sizeof(void*),PAGE_EXECUTE_READWRITE,&protection)!=0,"resource getter protection");
    *slot=reinterpret_cast<void*>(GetObject);
    Require(VirtualProtect(slot,sizeof(void*),protection,&unused)!=0,"resource getter protection restored");
}
inline void CheckChairs(void* manager) {
    CheckOverrides(factory);
    auto raw=Native(manager,SplitChairResources::LegacyRoot);auto legacy=Query(raw.v,PropertyIID());
    Require(legacy.p,"published chair collection loads");
    auto mergedValue=ModernCapeEffects::Resource(SplitChairResources::LegacyRoot,manager);
    auto merged=Query(mergedValue.v,PropertyIID());Require(merged.p,"combined chair collection loads");
    Require(Names(merged.p).size()==Names(legacy.p).size()+2,"native root enumeration includes exactly two split chairs");
    for(const auto& name:Names(legacy.p)) {
        Value before,after;Get(legacy.p,name,&before.v);Get(merged.p,name,&after.v);
        Require(MixedDyeResources::Unknown(before.v)==MixedDyeResources::Unknown(after.v),"all legacy chair objects preserved without cloning or replacement");
    }
    for(int id:{3018806,3019686}) {
        wchar_t key[20];swprintf_s(key,L"%08d",id);
        Value old;Get(legacy.p,key,&old.v);Require(SplitChairResources::Empty(old.v),"new chairs removed from legacy IMG");
        auto node=Resolve(merged.p,key);Require(Query(node.v,PropertyIID()).p,"root then item lookup finds split chairs");
        wchar_t direct[128],split[128];
        swprintf_s(direct,L"Item/Install/0301.img/%08d/info/icon",id);
        swprintf_s(split,L"Item/Install/%05d.img/%08d/info/icon",id/1000,id);
        auto expected=Native(manager,split),actual=ModernCapeEffects::Resource(direct,manager);
        auto a=Query(actual.v,CanvasIID()),b=Query(expected.v,CanvasIID());
        Require(a.p && b.p && Int(a.p,0x40)==Int(b.p,0x40) && Int(a.p,0x48)==Int(b.p,0x48),"native deep icon lookup resolves complete split canvas");
        Require(Pixel(a.p,0,0,Int(a.p,0x40),Int(a.p,0x48))==Pixel(b.p,0,0,Int(b.p,0x40),Int(b.p,0x48)),"native split icon pixels readable");
    }
    std::puts("PASS split chairs: root enumeration, every legacy object, new icons and deep paths through actual v83 PCOM");
}
}
