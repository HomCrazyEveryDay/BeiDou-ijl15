#pragma once
#include "MixedDye.h"
#include "DreamCanvas.h"
#include <oleauto.h>
#include <algorithm>
#include <string>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include <map>
#include <set>
#include <cstring>
#pragma comment(lib,"oleaut32.lib")

namespace MixedDyeResources {
using DreamCanvas::Method;
using DreamCanvas::Factory;
inline const GUID& PropertyIID(){static const GUID id={0x986515d9,0x0a0b,0x4929,{0x8b,0x4f,0x71,0x86,0x82,0x17,0x7b,0x92}};return id;}
inline const GUID& CanvasIID(){static const GUID id={0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};return id;}
inline const GUID& UolIID(){static const GUID id={0xf945bf59,0xd1ec,0x45e8,{0x8b,0xd9,0x3d,0xd1,0x1a,0xc1,0xa4,0x8a}};return id;}
inline const GUID& VectorIID(){static const GUID id={0xf28bd1ed,0x3deb,0x4f92,{0x9e,0xec,0x10,0xef,0x5a,0x1c,0x3f,0xb4}};return id;}
struct Object {
    void* p=nullptr;
    Object()=default;explicit Object(void* value):p(value){}
    Object(const Object&)=delete;Object& operator=(const Object&)=delete;
    Object(Object&& v) noexcept:p(v.p){v.p=nullptr;}
    ~Object(){DreamCanvas::Release(p);}
    void* detach(){void* v=p;p=nullptr;return v;}
};
struct Value {
    VARIANT v{};~Value(){VariantClear(&v);}
    Value()=default;Value(const Value&)=delete;Value& operator=(const Value&)=delete;
    Value(Value&& other) noexcept:v(other.v){VariantInit(&other.v);}
    Value& operator=(Value&& other) noexcept {if(this!=&other){VariantClear(&v);v=other.v;VariantInit(&other.v);}return *this;}
};
inline void Check(HRESULT hr){if(FAILED(hr))throw hr;}
inline IUnknown* Unknown(const VARIANT& v){return v.vt==VT_UNKNOWN?v.punkVal:v.vt==VT_DISPATCH?v.pdispVal:nullptr;}
inline Object Query(const VARIANT& v,const GUID& iid){Object out;auto u=Unknown(v);if(u)u->QueryInterface(iid,&out.p);return out;}
inline VARIANT Integer(int n){VARIANT v{};v.vt=VT_I4;v.lVal=n;return v;}
inline Object New(Factory factory,const wchar_t* name,const GUID& iid){Object p;Check(factory(name,&iid,&p.p,nullptr));if(!p.p)throw E_POINTER;return p;}
inline void Get(void* property,const std::wstring& name,VARIANT* out){
    BSTR key=SysAllocString(name.c_str());if(!key)throw E_OUTOFMEMORY;
    auto hr=Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT*)>(property,0x14)(property,key,out);SysFreeString(key);Check(hr);
}
inline void Put(void* property,const std::wstring& name,VARIANT value){
    BSTR key=SysAllocString(name.c_str());if(!key)throw E_OUTOFMEMORY;
    auto hr=Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT)>(property,0x18)(property,key,value);SysFreeString(key);Check(hr);
}
inline Object CanvasProperty(void* canvas){Object out;Check(Method<HRESULT(__stdcall*)(void*,void**)>(canvas,0x68)(canvas,&out.p));return out;}
inline std::vector<std::wstring> Names(void* property){
    Object enumerator;Check(Method<HRESULT(__stdcall*)(void*,void**)>(property,0x1c)(property,&enumerator.p));
    IEnumVARIANT* en=nullptr;Check(static_cast<IUnknown*>(enumerator.p)->QueryInterface(IID_IEnumVARIANT,reinterpret_cast<void**>(&en)));Object owned(en);
    std::vector<std::wstring> names;for(unsigned i=0;i<4096;++i){Value value;ULONG count=0;auto hr=en->Next(1,&value.v,&count);Check(hr);if(!count)return names;if(value.v.vt!=VT_BSTR)throw E_INVALIDARG;names.emplace_back(value.v.bstrVal);}
    throw E_INVALIDARG;
}
using ReadInt=HRESULT(__stdcall*)(void*,LONG*);
inline LONG Int(void* p,int offset){LONG result=0;Check(Method<ReadInt>(p,offset)(p,&result));return result;}
inline unsigned Pixel(void* p,int x,int y,int w,int h){if(!p || x<0 || y<0 || x>=w || y>=h)return 0;unsigned n=0;Check(Method<HRESULT(__stdcall*)(void*,int,int,unsigned*)>(p,0x88)(p,x,y,&n));return n;}
// Interpolate straight-alpha source art through premultiplied color weights.
// Equal opaque inputs keep alpha 255; transparent margins cannot darken edges.
inline unsigned MixPixel(unsigned a,unsigned b){
    const unsigned aa=a>>24,ba=b>>24,sum=aa+ba;if(!sum)return 0;
    unsigned out=((sum+1)/2)<<24;
    for(unsigned shift:{0u,8u,16u})out|=((((a>>shift)&255)*aa+((b>>shift)&255)*ba+sum/2)/sum)<<shift;
    return out;
}
struct Budget {std::size_t bytes=0;unsigned nodes=0,canvases=0,unmixedFrames=0;std::wstring path;};
inline Object BlendCanvas(void* a,void* b,Factory factory,Budget& budget){
    if(!a && !b)throw E_INVALIDARG;
    // A legitimately absent optional layer contributes transparent pixels. Its
    // bounds come from the present color; no source node or texture is changed.
    const int aw=Int(a?a:b,0x40),ah=Int(a?a:b,0x48),ax=Int(a?a:b,0x6c),ay=Int(a?a:b,0x74);
    const int bw=Int(b?b:a,0x40),bh=Int(b?b:a,0x48),bx=Int(b?b:a,0x6c),by=Int(b?b:a,0x74);
    if(aw<=0 || ah<=0 || bw<=0 || bh<=0 || aw>1024 || ah>1024 || bw>1024 || bh>1024
        || abs(ax)>2048 || abs(ay)>2048 || abs(bx)>2048 || abs(by)>2048)throw E_INVALIDARG;
    const int left=(std::min)(-ax,-bx),top=(std::min)(-ay,-by);
    const int w=(std::max)(aw-ax,bw-bx)-left,h=(std::max)(ah-ay,bh-by)-top;
    if(w<=0 || h<=0 || w>1024 || h>1024 || budget.bytes+std::size_t(w)*h*4>16*1024*1024)throw E_OUTOFMEMORY;
    auto out=New(factory,L"Canvas",CanvasIID());
    Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(out.p,0x2c)(out.p,w,h,Integer(0),Integer(2)));
    Check(Method<HRESULT(__stdcall*)(void*,LONG)>(out.p,0x54)(out.p,2));
    // DrawRectangle uses the old 4-bit alpha lookup table even on ARGB8888.
    // Lock only this newly created canvas and write exact straight-alpha pixels.
    const int tileW=Int(out.p,0x38),tileH=Int(out.p,0x3c);
    if(tileW<=0 || tileH<=0 || tileW>4096 || tileH>4096)throw E_INVALIDARG;
    for(int ty=0;ty<h;ty+=tileH)for(int tx=0;tx<w;tx+=tileW){
        Object raw;Check(Method<HRESULT(__stdcall*)(void*,int,int,void**)>(out.p,0x34)(out.p,tx,ty,&raw.p));if(!raw.p)throw E_POINTER;
        LONG pitch=0;Value address;
        Check(Method<HRESULT(__stdcall*)(void*,LONG*,VARIANT*)>(raw.p,0x1c)(raw.p,&pitch,&address.v));
        struct Unlock{void* raw;RECT rect;~Unlock(){Method<HRESULT(__stdcall*)(void*,RECT*)>(raw,0x20)(raw,&rect);}} unlock{raw.p,{0,0,(std::min)(tileW,w-tx),(std::min)(tileH,h-ty)}};
        if(address.v.vt!=(VT_BYREF|VT_UI4) || !address.v.byref || pitch<unlock.rect.right*4 || pitch>16384)throw E_INVALIDARG;
        auto pixels=reinterpret_cast<unsigned char*>(address.v.ulVal);
        for(int y=0;y<unlock.rect.bottom;++y)for(int x=0;x<unlock.rect.right;++x){
            unsigned color=MixPixel(Pixel(a,tx+x+left+ax,ty+y+top+ay,aw,ah),Pixel(b,tx+x+left+bx,ty+y+top+by,bw,bh));
            std::memcpy(pixels+y*pitch+x*4,&color,4);
        }
    }
    using WriteInt=HRESULT(__stdcall*)(void*,LONG);
    Check(Method<WriteInt>(out.p,0x70)(out.p,-left));Check(Method<WriteInt>(out.p,0x78)(out.p,-top));
    auto from=CanvasProperty(a?a:b),to=CanvasProperty(out.p);
    for(auto& key:Names(from.p)){if(key==L"origin")continue;Value value;Get(from.p,key,&value.v);Put(to.p,key,value.v);}
    // Canvas originX/Y setters do not create the WZ "origin" child. The v83
    // face compositor reads that child as IWzVector2D (409288), then throws
    // E_POINTER at 4093F5 if it is missing. Give the output its own vector;
    // never move the primary canvas's shared metadata to align two colors.
    auto origin=New(factory,L"Shape2D#Vector2D",VectorIID());
    Check(Method<WriteInt>(origin.p,0x24)(origin.p,-left));
    Check(Method<WriteInt>(origin.p,0x2c)(origin.p,-top));
    VARIANT originValue{};originValue.vt=VT_UNKNOWN;originValue.punkVal=static_cast<IUnknown*>(origin.p);
    Put(to.p,L"origin",originValue);
    budget.bytes+=std::size_t(w)*h*4;++budget.canvases;return out;
}
inline std::wstring Normalize(const std::wstring& path){
    std::vector<std::wstring> parts;std::size_t begin=0;
    while(begin<path.size()){
        auto end=path.find(L'/',begin);if(end==std::wstring::npos)end=path.size();auto part=path.substr(begin,end-begin);begin=end+1;
        if(part.empty() || part==L".")continue;
        if(part==L".."){if(parts.empty())throw E_INVALIDARG;parts.pop_back();}else parts.push_back(part);
    }
    std::wstring out;for(auto& part:parts){if(!out.empty())out+=L'/';out+=part;}return out;
}
inline Value Resolve(void* root,const std::wstring& path,unsigned hops=0){
    if(hops>24)throw E_INVALIDARG;
    Value current;current.v.vt=VT_UNKNOWN;current.v.punkVal=static_cast<IUnknown*>(root);current.v.punkVal->AddRef();
    std::wstring parent;std::size_t begin=0;
    while(begin<path.size()){
        auto end=path.find(L'/',begin);if(end==std::wstring::npos)end=path.size();auto key=path.substr(begin,end-begin);
        auto property=Query(current.v,PropertyIID());if(!property.p)throw E_INVALIDARG;
        Value next;Get(property.p,key,&next.v);
        // CMS 53513/56513 place the real default artwork under info.
        // Expose that existing node at the native path; never invent a color.
        if(parent.empty() && key==L"default" && (next.v.vt==VT_EMPTY || next.v.vt==VT_NULL)) {
            Value metadata;Get(property.p,L"info",&metadata.v);auto info=Query(metadata.v,PropertyIID());
            if(info.p){Value misplaced;Get(info.p,L"default",&misplaced.v);
                if(misplaced.v.vt!=VT_EMPTY && misplaced.v.vt!=VT_NULL)return Resolve(root,L"info/"+path,hops+1);}
        }
        auto link=Query(next.v,UolIID());
        if(link.p){
            BSTR raw=nullptr;Check(Method<HRESULT(__stdcall*)(void*,BSTR*)>(link.p,0x18)(link.p,&raw));
            std::wstring target=parent+(parent.empty()?L"":L"/")+(raw?raw:L"");SysFreeString(raw);
            if(end<path.size())target+=path.substr(end);
            return Resolve(root,Normalize(target),hops+1);
        }
        VariantClear(&current.v);current.v=next.v;VariantInit(&next.v);
        if(!parent.empty())parent+=L'/';parent+=key;begin=end+1;
    }
    return current;
}
// These aliases were checked against all available CMS 228.2 family colors.
// Resolve only inside the SAME color. The original IMG is never modified.
inline bool NeedsRopeAlias(int id){return id==40902 || id==43206 || id==44092;}
inline Value ResolveColor(void* root,int id,const std::wstring& path) {
    const wchar_t* from=nullptr;const wchar_t* to=nullptr;
    if(id==30542){from=L"swingP2/2";to=L"swingP2/1";}
    else if(NeedsRopeAlias(id)){from=L"rope";to=L"ladder";}
    else if(id==55549){from=L"default";to=L"blink/0";}
    if(from) {
        std::wstring prefix=from;
        if(path==prefix || (path.size()>prefix.size() && path.compare(0,prefix.size(),prefix)==0 && path[prefix.size()]==L'/')) {
            auto original=Resolve(root,prefix);
            if(original.v.vt==VT_EMPTY || original.v.vt==VT_NULL)return Resolve(root,to+path.substr(prefix.size()));
        }
    }
    return Resolve(root,path);
}
inline void CheckCategory(void* root,int id) {
    if(!id)return; // Legacy callers without an ID cannot enable compatibility rules.
    auto slot=Resolve(root,L"info/islot");
    const wchar_t* expected=MixedDye::IsFace(id)?L"Fc":MixedDye::IsHair(id)?L"Hr":L"";
    if(!*expected || slot.v.vt!=VT_BSTR || !slot.v.bstrVal || wcscmp(slot.v.bstrVal,expected))throw E_INVALIDARG;
}
inline bool KnownMissingFace(int id,const std::wstring& path,const VARIANT& value) {
    if((id==53116 && path==L"glitter/1/face") || (id==55763 && path==L"wink/0/face"))
        return value.vt==VT_EMPTY || value.vt==VT_NULL;
    if(id==56493 && path==L"glitter/1/face") {
        auto prop=Query(value,PropertyIID());if(!prop.p || Query(value,CanvasIID()).p)return false;
        auto keys=Names(prop.p);return keys.size()==1 && keys.front()==L"map";
    }
    return false;
}
struct CloneContext {void* rootA;void* rootB;int idA;int idB;std::map<std::pair<void*,void*>,Object> canvases;};
inline void PutObject(void* parent,const std::wstring& key,void* object) {
    VARIANT value{};value.vt=VT_UNKNOWN;value.punkVal=static_cast<IUnknown*>(object);Put(parent,key,value);
}
inline void* CachedBlend(void* a,void* b,Factory factory,Budget& budget,CloneContext& context) {
    auto ids=std::make_pair(a,b);auto found=context.canvases.find(ids);
    if(found==context.canvases.end())found=context.canvases.emplace(ids,BlendCanvas(a,b,factory,budget)).first;
    return found->second.p;
}
inline bool HairLayer(const std::wstring& key) {
    static const std::set<std::wstring> layers={L"hair",L"hairOverHead",L"hairBelowBody",L"hairShade",L"backHair",
        L"backHairBelowCap",L"backHairBelowCapWide",L"backHairBelowCapNarrow",L"hairOverHeadProne"};
    return layers.count(key)!=0;
}
inline bool Empty(const VARIANT& value){return value.vt==VT_EMPTY || value.vt==VT_NULL;}
inline bool NativeAction(const std::wstring& key) {
    static const std::set<std::wstring> actions={L"default",L"defaultProne",L"backDefault",L"stand1",L"stand2",L"walk1",L"walk2",
        L"alert",L"heal",L"prone",L"proneStab",L"jump",L"fly",L"ladder",L"rope",L"sit",L"shoot1",L"shoot2",L"shootF",
        L"swingO1",L"swingO2",L"swingO3",L"swingOF",L"swingT1",L"swingT2",L"swingT3",L"swingTF",L"swingP1",L"swingP2",L"swingPF",
        L"stabO1",L"stabO2",L"stabOF",L"stabT1",L"stabT2",L"stabTF",L"blink",L"hit",L"wink",L"glitter",L"smile",L"troubled",
        L"cry",L"angry",L"bewildered",L"stunned",L"vomit",L"oops",L"cheers",L"chu",L"pain",L"fear",L"despair",L"hum",L"bowing",L"hot",L"dam"};
    return actions.count(key)!=0;
}
// Original hairShade can be a canvas or a skin-indexed property, depending on
// the color. Normalize only this documented layer, retaining every skin entry.
inline Object BlendShade(const VARIANT& a,const VARIANT& b,Factory factory,Budget& budget,CloneContext& context,const std::wstring& path) {
    auto ac=Query(a,CanvasIID()),bc=Query(b,CanvasIID()),ap=Query(a,PropertyIID()),bp=Query(b,PropertyIID());
    auto out=New(factory,L"Property",PropertyIID());std::set<std::wstring> keys;
    if(ap.p && !ac.p)for(auto& key:Names(ap.p))keys.insert(key);
    if(bp.p && !bc.p)for(auto& key:Names(bp.p))keys.insert(key);
    for(auto& key:keys) {
        if(key.empty() || key.find_first_not_of(L"0123456789")!=std::wstring::npos)continue;
        Value av,bv;
        if(ac.p)Check(VariantCopy(&av.v,const_cast<VARIANT*>(&a)));
        else if(ap.p)av=ResolveColor(context.rootA,context.idA,path+L"/"+key);
        if(bc.p)Check(VariantCopy(&bv.v,const_cast<VARIANT*>(&b)));
        else if(bp.p)bv=ResolveColor(context.rootB,context.idB,path+L"/"+key);
        auto first=Query(av.v,CanvasIID()),second=Query(bv.v,CanvasIID());
        if((!first.p && !Empty(av.v)) || (!second.p && !Empty(bv.v)))throw E_INVALIDARG;
        if(first.p || second.p)PutObject(out.p,key,CachedBlend(first.p,second.p,factory,budget,context));
    }
    if(keys.empty() && (ac.p || bc.p))throw E_INVALIDARG;
    return out;
}
inline Object BlendTree(void* a,void* b,Factory factory,Budget& budget,CloneContext& context,const std::wstring& path,unsigned depth){
    if(depth>24 || ++budget.nodes>8192)throw E_INVALIDARG;
    auto out=New(factory,L"Property",PropertyIID());
    std::set<std::wstring> keys;for(auto& key:Names(a))keys.insert(key);for(auto& key:Names(b))keys.insert(key);
    // A plain repaired ID is composed with itself. Both raw trees lack rope,
    // so enumerating their union alone cannot expose the verified alias.
    if(path.empty() && (NeedsRopeAlias(context.idA) || NeedsRopeAlias(context.idB)))keys.insert(L"rope");
    if(path.empty()){auto first=ResolveColor(context.rootA,context.idA,L"default"),second=ResolveColor(context.rootB,context.idB,L"default");if(!Empty(first.v) || !Empty(second.v))keys.insert(L"default");}
    for(auto& key:keys){
        const auto childPath=path.empty()?key:path+L"/"+key;
        budget.path=childPath;
        Value first=ResolveColor(context.rootA,context.idA,childPath),second=ResolveColor(context.rootB,context.idB,childPath);
        // Some original assets contain dangling editor aliases in BOTH colors.
        // Native lookup treats them as absent; they carry no canvas to blend.
        if(Empty(first.v) && Empty(second.v))continue;
        // Metadata belongs to the primary style, including anchors and frame delays.
        if(key!=L"info"){
            auto ac=Query(first.v,CanvasIID());auto bc=Query(second.v,CanvasIID());
            auto ap=Query(first.v,PropertyIID());auto bp=Query(second.v,PropertyIID());
            if(!ac.p && !bc.p && !ap.p && !bp.p){if(!Empty(first.v))Put(out.p,key,first.v);continue;}
            if(key==L"hairShade" && ((ap.p && !ac.p) || (bp.p && !bc.p))) {
                auto child=BlendShade(first.v,second.v,factory,budget,context,childPath);PutObject(out.p,key,child.p);continue;
            }
            if(ac.p && bc.p){PutObject(out.p,key,CachedBlend(ac.p,bc.p,factory,budget,context));continue;}
            // These three source colors lack one expression canvas even in CMS.
            // Keep the available expression at full opacity, without inventing
            // pixels or fading half a face. This frame is explicitly NOT 50:50.
            if(key==L"face" && ((ac.p && KnownMissingFace(context.idB,childPath,second.v)) ||
                (bc.p && KnownMissingFace(context.idA,childPath,first.v)))) {
                void* canvas=ac.p?ac.p:bc.p;
                PutObject(out.p,key,CachedBlend(canvas,canvas,factory,budget,context));++budget.unmixedFrames;continue;
            }
            if(HairLayer(key) && ((ac.p && Empty(second.v)) || (bc.p && Empty(first.v)))) {
                PutObject(out.p,key,CachedBlend(ac.p,bc.p,factory,budget,context));continue;
            }
            if(ap.p && bp.p && !ac.p && !bc.p) {
                auto child=BlendTree(ap.p,bp.p,factory,budget,context,childPath,depth+1);PutObject(out.p,key,child.p);continue;
            }
            // Red 41951 has an original fourth stand1/stand2 frame. Preserve its art;
            // the other seven colors have no corresponding frame to mix.
            if((childPath==L"stand1/3" || childPath==L"stand2/3") && ((context.idA==41951 && context.idB>=41950 && context.idB<=41957 && ap.p && Empty(second.v)) ||
                (context.idB==41951 && context.idA>=41950 && context.idA<=41957 && bp.p && Empty(first.v)))) {
                auto root=ap.p?context.rootA:context.rootB;auto prop=ap.p?ap.p:bp.p;
                CloneContext single{root,root,41951,41951,{}};
                auto child=BlendTree(prop,prop,factory,budget,single,childPath,depth+1);
                PutObject(out.p,key,child.p);++budget.unmixedFrames;continue;
            }
            // Editor-only aliases and extra metadata do not participate in the
            // native pose. Keep primary values verbatim; never drop original art.
            // Missing action frames or the actual face/hair layer stay blocked.
            bool frame=!key.empty() && key.find_first_not_of(L"0123456789")==std::wstring::npos;
            if((key==L"face" && !path.empty()) || HairLayer(key) || frame || (path.empty() && NativeAction(key)))throw E_INVALIDARG;
        }
        if(!Empty(first.v))Put(out.p,key,first.v);
    }
    return out;
}
inline Object BlendProperty(void* a,void* b,Factory factory,Budget& budget,int idA=0,int idB=0){
    CheckCategory(a,idA);CheckCategory(b,idB);
    if((idA==0)!=(idB==0) || (idA && (MixedDye::IsFace(idA)!=MixedDye::IsFace(idB) ||
        MixedDye::WithColor(idA,0,MixedDye::IsFace(idA))!=MixedDye::WithColor(idB,0,MixedDye::IsFace(idB)))))throw E_INVALIDARG;
    CloneContext context{a,b,idA,idB,{}};return BlendTree(a,b,factory,budget,context,L"",0);
}
}
