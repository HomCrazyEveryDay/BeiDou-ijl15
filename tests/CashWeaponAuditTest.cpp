#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <fstream>
namespace CrashReporter {void RecordEvent(const char*,const char*,...) {}}
#include "ModernWeaponResources.h"
using namespace MixedDyeResources;
static void Require(bool v,const char* message){if(!v){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
static std::string Narrow(const std::wstring& s){std::string out;for(auto c:s)out.push_back(static_cast<char>(c));return out;}
int wmain(int argc,wchar_t** argv){
    Require(argc==4,"usage: audit.exe <client> <Data> <report.json>");
    std::ofstream report(argv[3]);Require(bool(report),"report opened");
    Require(SetCurrentDirectoryW(argv[1]),"client directory");
    Check(CoInitialize(nullptr));
    auto pcom=LoadLibraryW(L"PCOM.dll");Require(pcom,"PCOM");
    Check(reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(pcom,"PcInitModule"))());
    std::atexit([]{reinterpret_cast<void(__cdecl*)()>(GetProcAddress(GetModuleHandleW(L"PCOM.dll"),"PcTermModule"))();});
    auto factory=reinterpret_cast<Factory>(GetProcAddress(pcom,"PcCreateObject"));
    const GUID rmId={0x57dfe40b,0x3e20,0x4dbc,{0x97,0xe8,0x80,0x5a,0x50,0xf3,0x81,0xbf}};
    const GUID nsId={0x2aeeeb36,0xa4e1,0x4e2b,{0x8f,0x6f,0x2e,0x7b,0xde,0xc5,0xc5,0x3d}};
    const GUID fsId={0x352d8655,0x51e4,0x4668,{0x8c,0xe4,0x08,0x66,0xe2,0xb6,0xa5,0xb5}};
    auto rm=New(factory,L"ResMan",rmId),ns=New(factory,L"NameSpace",nsId),fs=New(factory,L"NameSpace#FileSystem",fsId);
    Check(Method<HRESULT(__stdcall*)(void*,int,int,int)>(rm.p,0x14)(rm.p,0x11,-1,-1));
    Check(reinterpret_cast<HRESULT(__cdecl*)(void**,int)>(GetProcAddress(pcom,"PcRootNameSpace"))(&ns.p,1));
    BSTR path=SysAllocString(argv[2]);Check(Method<HRESULT(__stdcall*)(void*,BSTR)>(fs.p,0x34)(fs.p,path));SysFreeString(path);
    path=SysAllocString(L"/");Check(Method<HRESULT(__stdcall*)(void*,BSTR,void*,int)>(ns.p,0x18)(ns.p,path,fs.p,0));SysFreeString(path);
    auto native=Method<ModernWeaponResources::Getter>(rm.p,0x1c);
    VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    WIN32_FIND_DATAW file{};auto search=std::wstring(argv[2])+L"/Character/Weapon/0170*.img";
    HANDLE files=FindFirstFileW(search.c_str(),&file);Require(files!=INVALID_HANDLE_VALUE,"weapons exist");
    int total=0,adapted=0,totalErrors=0;report<<"[\n";
    do {
        if(file.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
        int id=_wtoi(file.cFileName);std::wstring rootPath=L"Character/Weapon/"+std::wstring(file.cFileName);
        path=SysAllocString(rootPath.c_str());Value value;
        Check(ModernWeaponResources::GetObject(native,factory,rm.p,path,missing,missing,&value.v));SysFreeString(path);
        auto root=Query(value.v,PropertyIID());Require(root.p,"weapon root through production interceptor");
        auto cache=ModernWeaponResources::Cached().roots.find(rootPath);
        bool changed=cache!=ModernWeaponResources::Cached().roots.end() && cache->second.p;
        adapted+=changed;std::vector<int> types;std::vector<std::string> errors;
        int actions=0,frames=0,canvases=0;
        for(int type:{30,31,32,33,37,38,40,41,42,43,44,45,46,47,48,49}) {
            auto typeValue=Resolve(root.p,std::to_wstring(type));auto category=Query(typeValue.v,PropertyIID());
            if(!category.p)continue;types.push_back(type);
            for(const auto& action:Names(category.p)) {
                std::wstring actionPath=std::to_wstring(type)+L"/"+action;
                try {
                    auto av=Resolve(root.p,actionPath);auto ap=Query(av.v,PropertyIID());
                    if(!ap.p){errors.push_back(Narrow(actionPath));continue;}
                    ++actions;
                    for(const auto& frame:Names(ap.p)) {
                        if(!ModernWeaponResources::Numeric(frame))continue;
                        std::wstring framePath=actionPath+L"/"+frame;
                        auto fv=Resolve(root.p,framePath);auto fp=Query(fv.v,PropertyIID());
                        if(!fp.p){errors.push_back(Narrow(framePath));continue;}
                        ++frames;
                        for(const auto& part:Names(fp.p)) {
                            auto pv=Resolve(root.p,framePath+L"/"+part);
                            if(pv.v.vt==VT_EMPTY || pv.v.vt==VT_NULL){errors.push_back(Narrow(framePath+L"/"+part));continue;}
                            auto canvas=Query(pv.v,CanvasIID());if(!canvas.p)continue;
                            int w=Int(canvas.p,0x40),h=Int(canvas.p,0x48);
                            Require(w>0 && h>0,"native canvas dimensions");
                            // Decode native pixel storage as well as the node/UOL tree.
                            (void)Pixel(canvas.p,w/2,h/2,w,h);++canvases;
                        }
                    }
                } catch(HRESULT hr) {errors.push_back(Narrow(actionPath)+":HRESULT="+std::to_string(hr));}
            }
        }
        if(id==1702000)Require(types==std::vector<int>{33} && !changed,"dagger-only original not expanded");
        if(id==1702018)Require(types==std::vector<int>({33,37,38}) && !changed,"three-type original not expanded");
        if(id==1702204)Require(types==std::vector<int>{30} && !changed,"sword-only original not expanded");
        if(id==1702116 || id==1702117)Require(types==std::vector<int>({30,31,32,33,37,38}) && !changed,"both Chinese knots retain six original types and cannot become universal");
        if(total++)report<<",\n";
        report<<"{\"id\":"<<id<<",\"adapted\":"<<(changed?"true":"false")<<",\"types\":[";
        for(size_t i=0;i<types.size();++i){if(i)report<<',';report<<types[i];}
        report<<"],\"actions\":"<<actions<<",\"frames\":"<<frames<<",\"canvases\":"<<canvases<<",\"errors\":[";
        for(size_t i=0;i<errors.size();++i){if(i)report<<',';report<<'"'<<errors[i]<<'"';}
        report<<"]}";report.flush();totalErrors+=int(errors.size());
    } while(FindNextFileW(files,&file));
    FindClose(files);report<<"\n]\n";
    std::printf("Audited %d weapons through PCOM and production interception; adapted=%d unresolved=%d\n",total,adapted,totalErrors);
    return totalErrors?2:0;
}
