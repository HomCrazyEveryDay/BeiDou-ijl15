#define NOMINMAX
#include "../ezorsia/MonthlyShopText.h"
#include "../ezorsia/MonthlyShopTabs.h"
#include "../ezorsia/MonthlyShopTabCanvas.h"
#include <gdiplus.h>
#include <cstdio>
#include <cstdlib>
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdiplus.lib")
using namespace MixedDyeResources;
void Require(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}

// Canvas.dll has no renderer allocator outside the game and otherwise creates
// a single raw tile. Supply its native allocator interface (slot 0x28 setter,
// Allocate at 0x0c) while keeping every tile/pixel operation in real Canvas.dll.
// Also use a 16px tile height so the test exercises both axes and partial edges.
struct TileAllocator {
    Factory factory;LONG references=1;
    explicit TileAllocator(Factory f):factory(f){}
    virtual HRESULT __stdcall QueryInterface(REFIID iid,void** out){
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=IID_IUnknown)return E_NOINTERFACE;*out=this;AddRef();return S_OK;
    }
    virtual ULONG __stdcall AddRef(){return ++references;}
    virtual ULONG __stdcall Release(){return --references;}
    static HRESULT Set(void* canvas,TileAllocator* allocator){
        return Method<HRESULT(__stdcall*)(void*,void*)>(canvas,0x28)(canvas,allocator);
    }
    virtual HRESULT __stdcall Allocate(void* canvas,int format,int magnification){
        auto hr=Set(canvas,nullptr);if(FAILED(hr))return hr;
        struct Restore {void* canvas;TileAllocator* owner;~Restore(){Set(canvas,owner);}} restore{canvas,this};
        try{
            const int w=Int(canvas,0x40),h=Int(canvas,0x48);
            for(int y=0;y<h;y+=16)for(int x=0;x<w;x+=256){
                auto tile=New(factory,L"Canvas",CanvasIID());
                Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(tile.p,0x2c)(tile.p,(std::min)(256,w-x),(std::min)(16,h-y),Integer(magnification),Integer(format)));
                Object raw;Check(Method<HRESULT(__stdcall*)(void*,int,int,void**)>(tile.p,0x34)(tile.p,0,0,&raw.p));
                Check(Method<HRESULT(__stdcall*)(void*,int,int,void*)>(canvas,0x30)(canvas,x,y,raw.p));
            }
            return S_OK;
        }catch(HRESULT error){return error;}
    }
};

void Verify(Factory factory,const std::wstring& text,unsigned color,bool crossesTile){
    auto label=MonthlyShopText::Canvas(factory,text,color);
    Require(Int(label.p,0x40)==340 && Int(label.p,0x48)==17,"full text canvas dimensions");
    Require(Int(label.p,0x38)==256 && Int(label.p,0x3c)==16,"regression uses native raw tiles smaller than the label");
    // Exercise the same Copy ABI used by the NPC window, at its real offset.
    auto target=New(factory,L"Canvas",CanvasIID());
    Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(target.p,0x2c)(target.p,640,80,Integer(0),Integer(2)));
    constexpr unsigned background=0xffe7e8e4;
    Check(Method<HRESULT(__stdcall*)(void*,int,int,int,int,unsigned)>(target.p,0x8c)(target.p,0,0,640,80,background));
    Check(Method<HRESULT(__stdcall*)(void*,int,int,void*,VARIANT)>(target.p,0x80)(target.p,108,7,label.p,Integer(255)));
    unsigned first=0,second=0,seam=0,clear=0;
    for(int y=0;y<17;++y)for(int x=0;x<340;++x){
        auto pixel=Pixel(label.p,x,y,340,17),alpha=pixel>>24;
        Require(alpha==0 || alpha==255,"bitmap font alpha is binary");
        if(alpha){
            Require((pixel&0xffffff)==color,"text color preserved on both tiles");
            if(x<256)++first;else ++second;
            if(x>=252 && x<260)++seam;
        }else ++clear;
        Require(Pixel(target.p,x+108,y+7,640,80)==(alpha?pixel:background),"native window composition preserves text and transparent margins");
    }
    Require(clear>0,"text margins remain transparent");
    Require(text.empty()?first==0:first>0,"arrows, Chinese labels and empty text");
    Require(crossesTile?second>0 && seam>0:second==0,"long label continues across tile boundary, short label leaves second tile clear");
    Require(Pixel(target.p,107,7,640,80)==background && Pixel(target.p,448,7,640,80)==background,"copy stays within full canvas bounds");
}

void VerifyTabs(Factory factory,HMODULE module,const wchar_t* preview){
    using namespace MonthlyShopTabs;
    MonthlyShopModel::Snapshot snapshot;snapshot.month=202610;snapshot.channel=1;
    snapshot.months={{202610,1,"26年10月"},{202609,20,"26年9月"},{202608,20,"26年8月"},{All,1,"全部"}};
    auto cells=Cells(snapshot,0);Require(cells.size()==4,"All followed by three months in one row");
    for(const auto& cell:cells){
        Require(Hit(snapshot,0,cell.left,0)==cell.id && Hit(snapshot,0,cell.left+cell.width-1,Height-1)==cell.id,"entire native tab routes to the matching month");
        Require(Selectable(snapshot,cell.id,false)==(cell.id!=snapshot.month),"selected month does not send another request");
        Require(!Selectable(snapshot,cell.id,true),"pending request blocks duplicate switching");
    }
    for(auto p:{POINT{-1,10},POINT{222,10},POINT{100,-1},POINT{100,21},POINT{150,-60}})
        Require(Hit(snapshot,0,p.x,p.y)==None,"header, sell panel and item rows are outside month controls");
    auto extra=snapshot;extra.months.insert(extra.months.begin()+3,{202607,20,"7月上新"});extra.month=202607;
    Require(SelectedPage(extra)==1 && LastPage(4)==1,"additional configured months paginate");
    Require(Hit(extra,1,43,10)==Previous && Hit(extra,1,221,10)==Next,"paging arrows stay within native buy control");
    Require(Hit(extra,1,140,10)==202607,"last page month hit mapping");
    for(int y=0;y<Height;++y)for(int x=0;x<24;++x){
        Require(Hit(extra,1,42+x,y)==Previous && Hit(extra,1,Width-24+x,y)==Next,"full 24 by 21 arrow area is clickable");
    }
    const GUID rmid={0x57dfe40b,0x3e20,0x4dbc,{0x97,0xe8,0x80,0x5a,0x50,0xf3,0x81,0xbf}};
    const GUID nsid={0x2aeeeb36,0xa4e1,0x4e2b,{0x8f,0x6f,0x2e,0x7b,0xde,0xc5,0xc5,0x3d}};
    const GUID fsid={0x352d8655,0x51e4,0x4668,{0x8c,0xe4,0x08,0x66,0xe2,0xb6,0xa5,0xb5}};
    auto rm=New(factory,L"ResMan",rmid),ns=New(factory,L"NameSpace",nsid),fs=New(factory,L"NameSpace#FileSystem",fsid);
    Check(Method<HRESULT(__stdcall*)(void*,int,int,int)>(rm.p,0x14)(rm.p,0x11,-1,-1));
    Check(reinterpret_cast<HRESULT(__cdecl*)(void**,int)>(GetProcAddress(module,"PcRootNameSpace"))(&ns.p,1));
    BSTR path=SysAllocString(L"Data");Check(Method<HRESULT(__stdcall*)(void*,BSTR)>(fs.p,0x34)(fs.p,path));SysFreeString(path);
    path=SysAllocString(L"/");Check(Method<HRESULT(__stdcall*)(void*,BSTR,void*,int)>(ns.p,0x18)(ns.p,path,fs.p,0));SysFreeString(path);
    std::map<std::wstring,Object> assets;
    auto load=[&](const std::wstring& path)->void*{
        auto it=assets.find(path);if(it!=assets.end())return it->second.p;
        Value value;VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;BSTR key=SysAllocString(path.c_str());
        auto hr=Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT,VARIANT,VARIANT*)>(rm.p,0x1c)(rm.p,key,missing,missing,&value.v);SysFreeString(key);Check(hr);
        auto canvas=Query(value.v,CanvasIID());Require(canvas.p!=nullptr,"load native UI canvas");
        return assets.emplace(path,std::move(canvas)).first->second.p;
    };
    auto target=New(factory,L"Canvas",CanvasIID());
    Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(target.p,0x2c)(target.p,463,339,Integer(0),Integer(2)));
    MonthlyShopTabCanvas::Copy(target.p,load(L"UI/UIWindow.img/Shop/backgrnd"),0,0);
    for(int month=7;month>=1;--month)snapshot.months.insert(snapshot.months.end()-1,{202600+month,20,std::to_string(month)+"月"});
    snapshot.month=All;cells=Cells(snapshot,0);
    for(const auto& cell:cells){
        auto label=cell.id==All?L"全部":cell.id==Previous?L"<":cell.id==Next?L">":cell.id==202610?L"26年10月":L"26年9月";
        auto tab=MonthlyShopTabCanvas::Create(factory,load,label,cell.width,cell.id==snapshot.month);
        Require(Int(tab.p,0x40)==cell.width && Int(tab.p,0x48)==21,"native tab sprite bounds");
        MonthlyShopTabCanvas::Copy(target.p,tab.p,Left+cell.left,Top);
    }
    auto source=load(L"UI/UIWindow.img/Shop/backgrnd");
    for(auto p:{POINT{142,15},POINT{142,35},POINT{240,98},POINT{14,125}})
        Require(Pixel(target.p,p.x,p.y,463,339)==Pixel(source,p.x,p.y,463,339),"tabs leave header buttons, sell panel and item rows intact");
    Gdiplus::GdiplusStartupInput startup;ULONG_PTR token=0;
    Require(Gdiplus::GdiplusStartup(&token,&startup,nullptr)==Gdiplus::Ok,"preview encoder");
    {
        Gdiplus::Bitmap bitmap(463,339,PixelFormat32bppARGB);
        for(int y=0;y<339;++y)for(int x=0;x<463;++x)bitmap.SetPixel(x,y,Gdiplus::Color(Pixel(target.p,x,y,463,339)));
        const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        Require(bitmap.Save(preview,&png)==Gdiplus::Ok,"save offline shop preview");
    }
    Gdiplus::GdiplusShutdown(token);
}

int wmain(int argc,wchar_t** argv){
    Require(argc==3,"client directory and preview path arguments");
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    Require(SetCurrentDirectoryW(argv[1])!=0,"client directory");
    Require(SetDllDirectoryW(argv[1])!=0,"client DLL directory");
    Check(CoInitialize(nullptr));
    auto module=LoadLibraryW(L"PCOM.dll");Require(module!=nullptr,"load shipped PCOM.dll");
    auto init=reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(module,"PcInitModule"));Require(init!=nullptr,"PCOM init export");Check(init());
    auto factory=reinterpret_cast<Factory>(GetProcAddress(module,"PcCreateObject"));Require(factory!=nullptr,"PCOM factory");
    {
    TileAllocator allocator(factory);
    auto allocatorSettings=New(factory,L"Canvas",CanvasIID());
    Check(TileAllocator::Set(allocatorSettings.p,&allocator));
    struct Reset {void* canvas;~Reset(){TileAllocator::Set(canvas,nullptr);}} reset{allocatorSettings.p};
    try{
        for(auto label:{L"<",L">",L"8月上新",L"9月上新",L"10月上新",L"本页价格 ×20",L"价格已更新，请重新购买",L""})Verify(factory,label,0x303030,false);
        Verify(factory,L"10月上新",0xffffff,false);
        Verify(factory,L"点",0x835728,false);
        Verify(factory,L"本页价格 ×20  组队点数：9223372036854775807",0x303030,true);
        Verify(factory,std::wstring(100,L'M'),0xffffff,true);
        VerifyTabs(factory,module,argv[2]);
    }catch(HRESULT hr){std::fprintf(stderr,"FAIL: HRESULT %08lx\n",static_cast<unsigned long>(hr));return 1;}
    }
    CoUninitialize();
    std::puts("PASS: real PCOM/Canvas tiled text, native shop tab art, click boundaries, paging, pending selection, original UI isolation and offline preview");
}
