#define NOMINMAX
#include "../ezorsia/MixedDyeResources.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <set>
#pragma comment(lib,"ole32.lib")
using namespace MixedDyeResources;
#ifdef MIXED_DYE_HOOK_TEST
void CheckHook(void* manager,Factory factory);
#endif
void Require(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
void CheckCompatibility(void* rm,Factory factory);
Object Load(void* rm,const wchar_t* path){
    Value out;VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;BSTR s=SysAllocString(path);
    auto hr=Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT,VARIANT,VARIANT*)>(rm,0x1c)(rm,s,missing,missing,&out.v);SysFreeString(s);Check(hr);
    auto prop=Query(out.v,PropertyIID());Require(prop.p!=nullptr,"load real appearance property");return prop;
}
unsigned ValidatePixels(void* a,void* b,void* mixed,const std::wstring& prefix=L""){
    auto source=Resolve(a,prefix);auto prop=Query(source.v,PropertyIID());unsigned changed=0;
    for(auto& name:Names(prop.p)){
        if(name==L"info")continue;
        auto path=prefix.empty()?name:prefix+L"/"+name;
        auto av=Resolve(a,path),bv=Resolve(b,path),mv=Resolve(mixed,path);
        auto ac=Query(av.v,CanvasIID()),bc=Query(bv.v,CanvasIID()),mc=Query(mv.v,CanvasIID());
        if(ac.p){
            Require(bc.p && mc.p,"every frame resolves");
            auto metadata=CanvasProperty(mc.p);Value origin;Get(metadata.p,L"origin",&origin.v);
            Require(MixedDyeResources::Unknown(origin.v)!=nullptr,"mixed canvas preserves native origin vector");
            int aw=Int(ac.p,0x40),ah=Int(ac.p,0x48),ax=Int(ac.p,0x6c),ay=Int(ac.p,0x74);
            int bw=Int(bc.p,0x40),bh=Int(bc.p,0x48),bx=Int(bc.p,0x6c),by=Int(bc.p,0x74);
            int mw=Int(mc.p,0x40),mh=Int(mc.p,0x48),mx=Int(mc.p,0x6c),my=Int(mc.p,0x74);
            auto vector=Query(origin.v,VectorIID());Require(vector.p!=nullptr,"origin supports native IWzVector2D");
            Require(Int(vector.p,0x20)==mx && Int(vector.p,0x28)==my,"origin child matches aligned output canvas");
            auto originalMetadata=CanvasProperty(ac.p);Value sourceOrigin;Get(originalMetadata.p,L"origin",&sourceOrigin.v);
            auto sourceVector=Query(sourceOrigin.v,VectorIID());Require(sourceVector.p!=nullptr,"source origin vector");
            Require(sourceVector.p!=vector.p && Int(sourceVector.p,0x20)==ax && Int(sourceVector.p,0x28)==ay,"source origin remains independent and unchanged");
            for(int y=0;y<mh;++y)for(int x=0;x<mw;++x){
                unsigned ap=Pixel(ac.p,x-mx+ax,y-my+ay,aw,ah),bp=Pixel(bc.p,x-mx+bx,y-my+by,bw,bh);
                unsigned mp=Pixel(mc.p,x,y,mw,mh),expected=MixPixel(ap,bp);
                if(mp!=expected && ((mp|expected)>>24)){
                    std::printf("pixel (%d,%d) input=%08x,%08x actual=%08x expected=%08x\n",x,y,ap,bp,mp,expected);
                    Require(false,"native canvas pixel matches 50:50 including alpha");
                }
                changed+=mp!=ap && mp!=bp;
            }
        }else{auto nested=Query(av.v,PropertyIID());if(nested.p)changed+=ValidatePixels(a,b,mixed,path);}
    }
    return changed;
}
int wmain(int argc,wchar_t**argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e)->LONG {
        MEMORY_BASIC_INFORMATION memory{};VirtualQuery(e->ExceptionRecord->ExceptionAddress,&memory,sizeof(memory));
        wchar_t module[MAX_PATH]{};GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase),module,MAX_PATH);
        std::printf("EXCEPTION %08x address=%p offset=%zx\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress,reinterpret_cast<std::size_t>(e->ExceptionRecord->ExceptionAddress)-reinterpret_cast<std::size_t>(memory.AllocationBase));
        std::wprintf(L"module=%s\n",module);return EXCEPTION_EXECUTE_HANDLER;
    });
    setvbuf(stdout,nullptr,_IONBF,0);Require(argc==2 || argc==4,"client directory argument");
    Require(SetCurrentDirectoryW(argv[1]),"client directory");Check(CoInitialize(nullptr));
    auto module=LoadLibraryW(L"PCOM.dll");Require(module!=nullptr,"PCOM");
    Check(reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(module,"PcInitModule"))());
    auto factory=reinterpret_cast<Factory>(GetProcAddress(module,"PcCreateObject"));
    using Root=HRESULT(__cdecl*)(void**,int);auto root=reinterpret_cast<Root>(GetProcAddress(module,"PcRootNameSpace"));
    const GUID rmid={0x57dfe40b,0x3e20,0x4dbc,{0x97,0xe8,0x80,0x5a,0x50,0xf3,0x81,0xbf}};
    const GUID nsid={0x2aeeeb36,0xa4e1,0x4e2b,{0x8f,0x6f,0x2e,0x7b,0xde,0xc5,0xc5,0x3d}};
    const GUID fsid={0x352d8655,0x51e4,0x4668,{0x8c,0xe4,0x08,0x66,0xe2,0xb6,0xa5,0xb5}};
    auto rm=New(factory,L"ResMan",rmid),ns=New(factory,L"NameSpace",nsid),fs=New(factory,L"NameSpace#FileSystem",fsid);
    Check(Method<HRESULT(__stdcall*)(void*,int,int,int)>(rm.p,0x14)(rm.p,0x11,-1,-1));Check(root(&ns.p,1));
    wchar_t alternate[32768]{};GetEnvironmentVariableW(L"BEIDOU_MIXED_TEST_DATA",alternate,_countof(alternate));
    BSTR path=SysAllocString(*alternate?alternate:L"Data");Check(Method<HRESULT(__stdcall*)(void*,BSTR)>(fs.p,0x34)(fs.p,path));SysFreeString(path);
    path=SysAllocString(L"/");Check(Method<HRESULT(__stdcall*)(void*,BSTR,void*,int)>(ns.p,0x18)(ns.p,path,fs.p,0));SysFreeString(path);
    Require(MixPixel(0xffff0000,0xff0000ff)==0xff800080,"opaque equal blend");
    Require(MixPixel(0x00000000,0xffff0000)==0x80ff0000,"transparent edge keeps color");
    if(argc==4){
        wchar_t pairMode[2]{};const bool allPairs=GetEnvironmentVariableW(L"BEIDOU_MIXED_ALL_PAIRS",pairMode,2)>0;
        std::ifstream input(argv[2]);std::ofstream output(argv[3]);Require(input.good() && output.good(),"audit files");
        std::set<int> ids;int id;while(input>>id)ids.insert(id);
        for(int id:ids){
            bool face=MixedDye::IsFace(id);int baseline=0;
            for(int color=0;color<8;++color){int candidate=MixedDye::WithColor(id,color,face);if(ids.count(candidate)){baseline=candidate;break;}}
            Budget first,second;
            try{
                wchar_t pa[96],pb[96];swprintf_s(pa,L"Character/%s/%08d.img",face?L"Face":L"Hair",id);swprintf_s(pb,L"Character/%s/%08d.img",face?L"Face":L"Hair",baseline);
                auto a=Load(rm.p,pa),b=Load(rm.p,pb);
                auto forward=BlendProperty(a.p,b.p,factory,first),backward=BlendProperty(b.p,a.p,factory,second);
                if(!first.canvases || !second.canvases)throw E_INVALIDARG;
                if(allPairs)for(int color=0;color<8;++color) {
                    int candidate=MixedDye::WithColor(id,color,face);if(candidate==baseline || candidate==id || !ids.count(candidate))continue;
                    swprintf_s(pb,L"Character/%s/%08d.img",face?L"Face":L"Hair",candidate);auto other=Load(rm.p,pb);first={};second={};
                    auto pair=BlendProperty(a.p,other.p,factory,first);
                    if(!first.canvases)throw E_INVALIDARG;
                }
                output<<id<<" OK "<<first.canvases<<" "<<first.bytes<<"\n";
            }catch(HRESULT hr){
                auto path=second.path.empty()?first.path:second.path;
                int count=WideCharToMultiByte(CP_UTF8,0,path.c_str(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);
                std::string text(count,'\0');WideCharToMultiByte(CP_UTF8,0,path.c_str(),static_cast<int>(path.size()),text.data(),count,nullptr,nullptr);
                output<<id<<" FAIL "<<std::hex<<hr<<std::dec<<" "<<text<<"\n";
            }
            output.flush();
        }
        return 0;
    }
    for(int id:{30000,40590,63800,20000,53086,21303}){
        const bool face=MixedDye::IsFace(id);wchar_t pa[96],pb[96];
        swprintf_s(pa,L"Character/%s/%08d.img",face?L"Face":L"Hair",id);
        const int other=MixedDye::WithColor(id,1,face);swprintf_s(pb,L"Character/%s/%08d.img",face?L"Face":L"Hair",other);
        auto a=Load(rm.p,pa),b=Load(rm.p,pb);Budget budget;
        auto start=std::chrono::steady_clock::now();
        try{auto mixed=BlendProperty(a.p,b.p,factory,budget);Require(budget.canvases>0,"actual canvases mixed");Require(ValidatePixels(a.p,b.p,mixed.p)>0,"visible output differs from both source colors");}
        catch(HRESULT hr){std::printf("id=%d hr=%08x nodes=%u canvas=%u\n",id,hr,budget.nodes,budget.canvases);return 1;}
        std::printf("PASS real style %d + %d: canvases=%u bytes=%zu time=%lldms\n",id,other,budget.canvases,budget.bytes,std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());
    }
    CheckCompatibility(rm.p,factory);
#ifdef MIXED_DYE_HOOK_TEST
    CheckHook(rm.p,factory);
#endif
    return 0;
}

void CheckCompatibility(void* rm,Factory factory) {
    auto first=Load(rm,L"Character/Hair/00031420.img"),second=Load(rm,L"Character/Hair/00031421.img");Budget budget;
    auto output=BlendProperty(first.p,second.p,factory,budget);
    auto a=Resolve(first.p,L"walk1/0/hairShade/0"),b=Resolve(second.p,L"walk1/0/hairShade"),m=Resolve(output.p,L"walk1/0/hairShade/0");
    auto ac=Query(a.v,CanvasIID()),bc=Query(b.v,CanvasIID()),mc=Query(m.v,CanvasIID());
    Require(ac.p && !bc.p && mc.p,"missing optional shade blends against transparent, keeps original node");
    unsigned changed=0;int w=Int(ac.p,0x40),h=Int(ac.p,0x48);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto source=Pixel(ac.p,x,y,w,h),mixed=Pixel(mc.p,x,y,w,h);Require(mixed==MixPixel(source,0) || ((mixed|source)>>24)==0,"missing layer has exactly half source alpha");changed+=(source>>24)!=0;}
    Require(changed>20,"actual visible shade tested");
    for(int id:{53513,56513}) {
        wchar_t path[96];swprintf_s(path,L"Character/Face/%08d.img",id);auto base=Load(rm,path);
        swprintf_s(path,L"Character/Face/%08d.img",MixedDye::WithColor(id,0,true));auto other=Load(rm,path);Budget used;
        auto mixed=BlendProperty(base.p,other.p,factory,used);auto face=Resolve(mixed.p,L"default/face");
        Require(Query(face.v,CanvasIID()).p!=nullptr,"misplaced info/default uses real original canvas");
    }
    std::puts("PASS asymmetric original shade pixels and misplaced original default faces");
}
