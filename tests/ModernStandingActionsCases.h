#pragma once
#include "ModernStandingActions.h"

void CheckStandingAndRotation(Factory factory,void* manager) {
    using namespace ModernCapeEffects;
    BYTE avatar[0x1100]{};
    for(int item:{1053910,1053909}) {
        std::memcpy(avatar+0x31,&item,4);
        Require(ModernStandingActions::Select(2,avatar,manager)==167 &&
                ModernStandingActions::Select(3,avatar,manager)==168,"both sports outfits select both original floating stances");
        for(int action:{0,1,4,5,12,31,32,33,34,35,36,37,39,139,162,166})
            Require(ModernStandingActions::Select(action,avatar,manager)==action,"walking, skills, sitting, prone, climbing and Evan stay native");
        for(int offset:{0x4a0,0x4b4}) {
            *reinterpret_cast<int*>(avatar+offset)=1;
            Require(ModernStandingActions::Select(2,avatar,manager)==2,"mounts/morphs keep their original bank");
            *reinterpret_cast<int*>(avatar+offset)=0;
        }
    }
    std::memset(avatar+0x31,0,4);
    Require(ModernStandingActions::Select(2,avatar,manager)==2,"unequipping restores native standing");
    const int expectedFrames[]={0,0,1,1,2,2,1,0},expectedY[]={-1,-2,-3,-4,-3,-2,-1,0};
    for(const wchar_t* base:{L"stand1",L"stand2"}) {
        const std::wstring path=std::wstring(L"Character/00002000.img/")+base+L"_floating";
        auto resource=Resource(path,manager);auto action=Query(resource.v,PropertyIID());
        Require(action.p && Names(action.p).size()==8,"eight complete original movement frames");
        for(int i=0;i<8;++i) {
            auto v=Resolve(action.p,std::to_wstring(i));auto frame=Query(v.v,PropertyIID());
            auto name=Resolve(frame.p,L"action"),move=Resolve(frame.p,L"move");auto vector=Query(move.v,VectorIID());
            Require(name.v.vt==VT_BSTR && !wcscmp(name.v.bstrVal,base),"floating uses the original equipment pose");
            Require(Number(frame.p,L"delay",0)==120 && Number(frame.p,L"frame",-1)==expectedFrames[i] &&
                    vector.p && Int(vector.p,0x20)==0 && Int(vector.p,0x28)==expectedY[i],"original 960 ms movement/pose cycle preserved");
            auto direct=Resolve(frame.p,L"body");
            Require(!Query(direct.v,CanvasIID()).p,"floating metadata is not a direct body canvas bank");
            const std::wstring pose=std::wstring(name.v.bstrVal)+L"/"+std::to_wstring(Number(frame.p,L"frame",-1));
            const std::pair<const wchar_t*,const wchar_t*> parts[]={
                {L"00002000.img",L"body"},{L"00002000.img",L"arm"},{L"00012000.img",L"head"},
                {L"Longcoat/01053909.img",L"mail"},{L"Longcoat/01053909.img",L"mailArm"},
                {L"Longcoat/01053910.img",L"mail"},{L"Longcoat/01053910.img",L"mailArm"}};
            for(const auto& part:parts) {
                auto pixels=Resource(std::wstring(L"Character/")+part.first+L"/"+pose+L"/"+part.second,manager);
                auto canvas=Query(pixels.v,CanvasIID());
                Require(canvas.p,"native frame references resolve body/head/arms and both original outfits");
                const int width=Int(canvas.p,0x40),height=Int(canvas.p,0x48);
                bool visible=false;
                for(int y=0;y<height && !visible;++y)for(int x=0;x<width && !visible;++x)
                    visible=(Pixel(canvas.p,x,y,width,height)>>24)!=0;
                Require(visible,"referenced standing part contains visible pixels");
            }
        }
    }
    for(const wchar_t* action:{L"somersault",L"octopus",L"tripleSwing",L"fullSwingTriple",L"prone"})
        Require(IsRotationAction(action,manager),"whole special action is classified from original rotate/prone metadata");
    for(const wchar_t* action:{L"stand1",L"stand2",L"walk1",L"jump",L"ladder",L"rope",L"stand1_floating"})
        Require(!IsRotationAction(action,manager),"normal movement and floating do not suppress effects");
    auto body=New(factory,L"Shape2D#Vector2D",VectorIID()),head=New(factory,L"Shape2D#Vector2D",VectorIID());
    Layer overlay{layerMethods},face{layerMethods};void** graph=graphMethods;
    *reinterpret_cast<void**>(avatar+0x10b4)=head.p;*reinterpret_cast<void**>(avatar+0x10b8)=body.p;
    *reinterpret_cast<void**>(avatar+0x10c0)=&face;*reinterpret_cast<void**>(avatar+0x10c8)=&overlay;
    for(int item:{1073379,1022241,1022242,1053910,1053909,1074003,1103654,1103649}) {
        State state;state.id=item;auto effect=EffectFor(item,manager);state.effect.p=effect.detach();
        Sync(state,&graph,avatar,&overlay,L"stand1",0);Require(state.layer.p,"effect present before special action");
        Sync(state,&graph,avatar,&overlay,L"somersault",1,IsRotationAction(L"somersault",manager));
        const bool exceptRotation=item==1073379 || item==1022241 || item==1022242;
        Require((state.layer.p==nullptr)==exceptRotation,"only source-marked effects removed during rotating actions");
        Sync(state,&graph,avatar,&overlay,L"stand1",1);
        Require(state.layer.p && static_cast<Layer*>(state.layer.p)->flip==1 && !state.failed,"animation and facing recover after special action");
        Sync(state,&graph,avatar,&overlay,L"prone",0,true);
        Require((state.layer.p==nullptr)==exceptRotation,"source-marked effects removed during prone");
    }
    Require(live==0,"special-action transitions release all layers");
    std::puts("PASS original floating stances: both items/stances, 16 poses/offsets, 112 referenced body/head/arm/outfit canvases, 960 ms cycles; native movement/mount/morph preservation; rotation/prone effect suppression and recovery");
}
