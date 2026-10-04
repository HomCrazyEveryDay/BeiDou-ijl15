#include "stdafx.h"
#include "MixedDyeResources.h"
#include "ChairImageLinks.h"
#include "CrashReporter.h"
#include <list>
#include <mutex>
#include <cstring>

namespace MixedDye {
namespace {
using namespace MixedDyeResources;
using GetObject=HRESULT(__stdcall*)(void*,BSTR,VARIANT,VARIANT,VARIANT*);
GetObject originalGetObject=nullptr;
Factory factory=nullptr;
struct Entry {std::uint32_t id;Object image;std::size_t bytes;};
// Intentionally process-lifetime: COM components can unload before ijl15 during
// exit. Active cache ownership is bounded; eviction releases references normally.
struct Cache {std::recursive_mutex mutex;std::list<Entry> entries;std::size_t bytes=0;};
Cache& Images(){static auto* cache=new Cache;return *cache;}
thread_local bool building=false;
LONG reports=0;
using CashAllowed=int(__cdecl*)(int);
CashAllowed nativeCashAllowed=reinterpret_cast<CashAllowed>(0x004863D5);
int __cdecl CouponAllowed(int id){return IsCoupon(id)?1:nativeCashAllowed(id);}

bool ParsePath(BSTR input,std::uint32_t& id,Style& style,std::wstring& suffix){
    if(!input || SysStringLen(input)>256)return false;
    const wchar_t* p=input;if(*p==L'/')++p;if(wcsncmp(p,L"Data/",5)==0)p+=5;
    bool face=false;
    if(wcsncmp(p,L"Character/Hair/",15)==0)p+=15;
    else if(wcsncmp(p,L"Character/Face/",15)==0){p+=15;face=true;}else return false;
    if(*p<L'0' || *p>L'9')return false;
    wchar_t* end=nullptr;unsigned long parsed=wcstoul(p,&end,10);
    if(!end || wcsncmp(end,L".img",4) || (end[4] && end[4]!=L'/'))return false;
    if(!Decode(parsed,style)) {
        // GM !hair, ordinary hair and pre-mix previews use real IDs. Repair the
        // same verified back-pose aliases here too; never mutate shared IMG data.
        if(face || !NeedsRopeAlias(static_cast<int>(parsed)))return false;
        style={static_cast<int>(parsed),static_cast<int>(parsed),false};
    }
    if(style.face!=face)return false;
    id=parsed;suffix=end+4;if(!suffix.empty())suffix.erase(0,1);return true;
}
Object LoadBase(void* rm,int id,bool face){
    wchar_t path[80];swprintf_s(path,L"Character/%s/%08d.img",face?L"Face":L"Hair",id);
    BSTR name=SysAllocString(path);if(!name)throw E_OUTOFMEMORY;
    Value value;VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    auto hr=originalGetObject(rm,name,missing,missing,&value.v);SysFreeString(name);Check(hr);
    auto property=Query(value.v,PropertyIID());if(!property.p)throw E_NOINTERFACE;return property;
}
HRESULT __stdcall GetMixedObject(void* rm,BSTR path,VARIANT a,VARIANT b,VARIANT* result){
    std::uint32_t id=0;Style style;std::wstring suffix;
    if(!ParsePath(path,id,style,suffix))return ChairImageLinks::GetObject(originalGetObject,factory,rm,path,a,b,result);
    if(!result)return E_POINTER;
    try{
        if(building)throw E_UNEXPECTED;
        auto& cache=Images();std::lock_guard<std::recursive_mutex> lock(cache.mutex);
        auto found=std::find_if(cache.entries.begin(),cache.entries.end(),[id](const Entry& e){return e.id==id;});
        if(found==cache.entries.end()){
            struct Building{Building(){building=true;}~Building(){building=false;}} guard;
            auto first=LoadBase(rm,style.primary,style.face),second=LoadBase(rm,style.secondary,style.face);Budget budget;
            auto image=BlendProperty(first.p,second.p,factory,budget,style.primary,style.secondary);if(!budget.canvases)throw E_INVALIDARG;
            while(!cache.entries.empty() && (cache.entries.size()>=128 || cache.bytes+budget.bytes>32*1024*1024)){
                cache.bytes-=cache.entries.back().bytes;cache.entries.pop_back();
            }
            cache.bytes+=budget.bytes;cache.entries.push_front({id,std::move(image),budget.bytes});found=cache.entries.begin();
        }else cache.entries.splice(cache.entries.begin(),cache.entries,found);
        auto& image=cache.entries.front().image;
        if(suffix.empty()){VariantInit(result);result->vt=VT_UNKNOWN;result->punkVal=static_cast<IUnknown*>(image.p);result->punkVal->AddRef();}
        else{Value value=Resolve(image.p,suffix);Check(VariantCopy(result,&value.v));}
        return S_OK;
    }catch(...){
        if(InterlockedIncrement(&reports)<=8)CrashReporter::RecordEvent("appearance.mix","resource_failed id=%u primary=%d secondary=%d",id,style.primary,style.secondary);
        // Preserve a usable appearance on resource failure; never return a null
        // mixed property into native avatar rendering or alter the source canvas.
        wchar_t base[80];swprintf_s(base,L"Character/%s/%08d.img",style.face?L"Face":L"Hair",style.primary);
        std::wstring fallback=base;if(!suffix.empty())fallback+=L"/"+suffix;
        BSTR name=SysAllocString(fallback.c_str());if(!name)return E_OUTOFMEMORY;
        auto hr=originalGetObject(rm,name,a,b,result);SysFreeString(name);return hr;
    }
}

// Only normalize the classification. The native formatter still receives the
// complete encoded ID, with hair and face using disjoint resource categories.
int __stdcall OriginalStyle(int id){Style style;return Decode(id,style)?(style.face?20000:30000):id;}
DWORD classifyResume=0x005C94C3;
DWORD beautyClassifyResume=0x009ACA9B;
// CUtilDlgEx::SetAvatar classifies the first choice separately from the resource
// loader. Unrecognized categories become skin (2) and overwrite AvatarLook+0xD.
// Normalize here before native mode selection; retain encoded IDs in the list.
__declspec(naked) void ClassifyBeauty(){
    __asm{
        push eax
        call OriginalStyle
        cdq
        mov ecx,10000
        idiv ecx
        jmp dword ptr[beautyClassifyResume]
    }
}
__declspec(naked) void ClassifyResource(){
    __asm{
        push ecx
        push edx
        push dword ptr[ebp+0Ch]
        call OriginalStyle
        pop edx
        pop ecx
        cdq
        mov ecx,10000
        idiv ecx
        jmp dword ptr[classifyResume]
    }
}
}

bool AttachResourceManager(void* manager){
    if(!manager)return false;if(originalGetObject)return true;
    auto module=GetModuleHandleW(L"PCOM.dll");factory=reinterpret_cast<Factory>(GetProcAddress(module,"PcCreateObject"));
    if(!factory)return false;
    originalGetObject=Method<GetObject>(manager,0x1c);
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&originalGetObject),GetMixedObject)){originalGetObject=nullptr;return false;}
    return true;
}
bool Install(){
    const BYTE expected[]={0x8b,0x45,0x0c,0x99,0xb9,0x10,0x27,0,0,0xf7,0xf9};
    auto target=reinterpret_cast<BYTE*>(0x005C94B8);
    if(std::memcmp(target,expected,sizeof(expected)))return false;
    // Select the native scripted-cash branch (4EFEB7 -> 4F05A9 -> A0A63F),
    // not the later local-only cash UI branch through A1DC5B.
    const BYTE cashExpected[]={0xe8,0x23,0x65,0xf9,0xff};
    auto cashTarget=reinterpret_cast<BYTE*>(0x004EFEAD);
    if(std::memcmp(cashTarget,cashExpected,sizeof(cashExpected)))return false;
    const BYTE beautyExpected[]={0x99,0xb9,0x10,0x27,0,0,0xf7,0xf9};
    auto beautyTarget=reinterpret_cast<BYTE*>(0x009ACA93);
    if(std::memcmp(beautyTarget,beautyExpected,sizeof(beautyExpected)))return false;
    // CUIItem::Draw uses the same legacy cash predicate to decide whether to
    // read GW_ItemSlotBase::GetQuantity and draw the native number sprites.
    // Admit only these two coupons at this call site; item quantities are untouched.
    const BYTE countExpected[]={0xe8,0xca,0x84,0xc6,0xff};
    auto countTarget=reinterpret_cast<BYTE*>(0x0081DF06);
    if(std::memcmp(countTarget,countExpected,sizeof(countExpected)))return false;
    BYTE patch[sizeof(expected)];std::memset(patch,0x90,sizeof(patch));patch[0]=0xe9;
    const DWORD relative=reinterpret_cast<DWORD>(ClassifyResource)-reinterpret_cast<DWORD>(target)-5;std::memcpy(patch+1,&relative,4);
    BYTE beautyPatch[sizeof(beautyExpected)];std::memset(beautyPatch,0x90,sizeof(beautyPatch));beautyPatch[0]=0xe9;
    const DWORD beautyJump=reinterpret_cast<DWORD>(ClassifyBeauty)-reinterpret_cast<DWORD>(beautyTarget)-5;std::memcpy(beautyPatch+1,&beautyJump,4);
    BYTE cashPatch[5]={0xe8};const DWORD cashCall=reinterpret_cast<DWORD>(CouponAllowed)-reinterpret_cast<DWORD>(cashTarget)-5;std::memcpy(cashPatch+1,&cashCall,4);
    BYTE countPatch[5]={0xe8};const DWORD countCall=reinterpret_cast<DWORD>(CouponAllowed)-reinterpret_cast<DWORD>(countTarget)-5;std::memcpy(countPatch+1,&countCall,4);
    struct Patch {BYTE* site;const BYTE* before;const BYTE* after;SIZE_T size;DWORD protection=0;};
    Patch patches[]={{target,expected,patch,sizeof(patch)}, {cashTarget,cashExpected,cashPatch,sizeof(cashPatch)}, {beautyTarget,beautyExpected,beautyPatch,sizeof(beautyPatch)}, {countTarget,countExpected,countPatch,sizeof(countPatch)}};
    DWORD ignored=0;unsigned prepared=0;
    // Startup-only: acquire all pages before changing any of the four paths.
    for(auto& p:patches){
        if(!VirtualProtect(p.site,p.size,PAGE_EXECUTE_READWRITE,&p.protection)){
            for(unsigned i=0;i<prepared;++i)VirtualProtect(patches[i].site,patches[i].size,patches[i].protection,&ignored);
            return false;
        }
        ++prepared;
    }
    bool ok=true;
    for(auto& p:patches){std::memcpy(p.site,p.after,p.size);if(!FlushInstructionCache(GetCurrentProcess(),p.site,p.size))ok=false;}
    if(!ok)for(auto& p:patches){std::memcpy(p.site,p.before,p.size);FlushInstructionCache(GetCurrentProcess(),p.site,p.size);}
    for(auto& p:patches)if(!VirtualProtect(p.site,p.size,p.protection,&ignored))ok=false;
    return ok;
}
void SendCoupon(int position,int id){
    if(!IsCoupon(id) || position<=0 || position>32767)return;
    void* socket=*reinterpret_cast<void**>(0x00BE7914);if(!socket)return;
    // Exactly the v83 USE_CASH_ITEM body expected by UseCashItemHandler.
    struct Packet{int loopback;unsigned char* data;unsigned long size;unsigned offset;int encrypted;};
    unsigned char payload[8]={0x4f,0};short slot=static_cast<short>(position);
    std::memcpy(payload+2,&slot,2);std::memcpy(payload+4,&id,4);Packet packet{0,payload,8,0,0};
    reinterpret_cast<void(__fastcall*)(void*,void*,Packet*)>(0x0049637B)(socket,nullptr,&packet);
}
}
