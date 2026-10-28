#pragma once
#include "MixedDyeResources.h"
#include "CrashReporter.h"
#include <mutex>

// CItemInfo::GetCharacterImg (5C94A1) returns a resource property which the
// v83 action loader indexes by the equipped weapon's type. Modern universal
// cash weapons instead store the common actions under 30 and gun art under 49.
// Supply that schema in memory; retain the original IMG and each real type.
namespace ModernWeaponResources {
using namespace MixedDyeResources;
using Getter = HRESULT(__stdcall*)(void*, BSTR, VARIANT, VARIANT, VARIANT*);
inline bool Match(BSTR path, std::wstring& root, std::wstring& suffix) {
    if (!path || SysStringLen(path)<28 || SysStringLen(path)>256) return false;
    const wchar_t* p=path;
    if (*p==L'/') ++p;
    if (wcsncmp(p,L"Data/",5)==0) p+=5;
    constexpr wchar_t prefix[]=L"Character/Weapon/0170";
    constexpr size_t n=_countof(prefix)-1;
    if(wcsncmp(p,prefix,n))return false;
    for(size_t i=n;i<n+4;++i)if(p[i]<L'0'||p[i]>L'9')return false;
    if(wcsncmp(p+n+4,L".img",4) || (p[n+8] && p[n+8]!=L'/'))return false;
    root.assign(p,n+8);suffix=p[n+8]?p+n+9:L"";return true;
}
inline bool Numeric(const std::wstring& s){return !s.empty() && s.find_first_not_of(L"0123456789")==std::wstring::npos;}
inline Object Adapt(void* original,Factory factory) {
    const auto names=Names(original);
    std::set<std::wstring> types;
    for(const auto& key:names)if(Numeric(key))types.insert(key);
    // This exact modern schema and the complete swing/shoot repertoire distinguish
    // a universal weapon from an old sticker restricted to one weapon category.
    if(types!=std::set<std::wstring>{L"30",L"49"})return Object();
    auto cash=Resolve(original,L"info/cash"),slot=Resolve(original,L"info/islot");
    if(cash.v.vt!=VT_I4 || cash.v.lVal!=1 || slot.v.vt!=VT_BSTR || wcscmp(slot.v.bstrVal,L"Wp"))return Object();
    for(const auto* action:{L"stand1",L"stand2",L"swingO1",L"swingT1",L"swingP1",L"stabO1",L"shoot1"}) {
        auto value=Resolve(original,std::wstring(L"30/")+action);
        if(!Query(value.v,PropertyIID()).p)return Object();
    }
    auto common=Resolve(original,L"30");
    if(!Query(common.v,PropertyIID()).p)return Object();
    auto out=New(factory,L"Property",PropertyIID());
    for(const auto& key:names){Value value;Get(original,key,&value.v);Put(out.p,key,value.v);}
    for(const auto* key:{L"31",L"32",L"33",L"37",L"38",L"40",L"41",L"42",L"43",L"44",L"45",L"46",L"47",L"48"})
        Put(out.p,key,common.v);
    return out;
}
struct Cache {std::recursive_mutex mutex;void* manager=nullptr;std::map<std::wstring,Object> roots;};
inline Cache& Cached(){static auto* p=new Cache;return *p;}
inline HRESULT GetObject(Getter native,Factory factory,void* manager,BSTR path,VARIANT a,VARIANT b,VARIANT* result) {
    static thread_local bool building=false;
    if(building)return native(manager,path,a,b,result);
    std::wstring root,suffix;
    if(!Match(path,root,suffix))return native(manager,path,a,b,result);
    if(!result)return E_POINTER;
    try {
        auto& cache=Cached();std::lock_guard<std::recursive_mutex> lock(cache.mutex);
        if(cache.manager!=manager){cache.roots.clear();cache.manager=manager;}
        auto found=cache.roots.find(root);
        if(found==cache.roots.end()) {
            struct Guard {bool& flag;Guard(bool& v):flag(v){flag=true;}~Guard(){flag=false;}} guard(building);
            BSTR name=SysAllocString(root.c_str());if(!name)throw E_OUTOFMEMORY;
            Value value;HRESULT hr=native(manager,name,a,b,&value.v);SysFreeString(name);Check(hr);
            auto original=Query(value.v,PropertyIID());if(!original.p)throw E_NOINTERFACE;
            auto replacement=Adapt(original.p,factory);
            if(cache.roots.size()>=256)cache.roots.clear();
            found=cache.roots.emplace(root,std::move(replacement)).first;
            if(found->second.p) {
                static LONG reports=0;
                if(InterlockedIncrement(&reports)<=32)CrashReporter::RecordEvent("equipment.weapon-schema","modern common actions adapted; source unchanged");
            }
        }
        if(!found->second.p)return native(manager,path,a,b,result);
        auto value=Resolve(found->second.p,suffix);
        // Deep canvas metadata lookups retain native behavior if the property
        // reader cannot traverse a canvas. The root/type lookups are the fix.
        VariantInit(result);Check(ResourceVariant::Copy(result,&value.v));return S_OK;
    } catch(...) {return native(manager,path,a,b,result);}
}
}
