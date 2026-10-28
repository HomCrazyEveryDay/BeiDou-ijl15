#pragma once
#include "ModernSetEffects.h"

void CheckSchoolSets(Factory factory,void* manager) {
    using namespace ModernSetEffects;
    auto& catalog=Resources();Load(catalog,manager);
    Require(catalog.loaded && catalog.definitions.size()==2,"discover both source random sets without an item allowlist");
    void** graph=graphMethods;Layer overlay{layerMethods};
    auto body=New(factory,L"Shape2D#Vector2D",VectorIID()),head=New(factory,L"Shape2D#Vector2D",VectorIID());
    BYTE avatar[0x1100]{};
    *reinterpret_cast<void**>(avatar+0x10b4)=head.p;*reinterpret_cast<void**>(avatar+0x10b8)=body.p;
    *reinterpret_cast<void**>(avatar+0x10c8)=&overlay;
    auto equip=[&](int coat,int shoes){std::memcpy(avatar+0x31,&coat,4);std::memcpy(avatar+0x39,&shoes,4);};
    const int frameCounts[2][4]={{6,10,5,13},{8,8,8,10}};
    const int durations[2][4]={{900,1500,900,1950},{1200,1200,1200,1500}};
    int totalFrames=0,totalVariants=0;
    for(const auto& definition:catalog.definitions) {
        const int group=definition.id==152?0:1;
        Require(definition.id==152 || definition.id==153,"verified source set identities");
        Require(definition.required.size()==2 && definition.actions.size()==2,"source requires coat AND shoes and declares two standing actions");
        for(const auto& action:definition.actions) {
            Require((action.first==L"stand1" || action.first==L"stand2") && action.second.size()==4,"four original companions per stance");
            for(int variant=0;variant<4;++variant) {
                auto sequence=action.first+L"/randEffect/"+std::to_wstring(variant);
                ModernCapeEffects::State render;render.effect.p=definition.effect.p;static_cast<IUnknown*>(render.effect.p)->AddRef();
                ModernCapeEffects::SyncSequence(render,&graph,avatar,&overlay,sequence,0,action.first);
                auto layer=static_cast<Layer*>(render.layer.p);Require(layer && layer->frames==frameCounts[group][variant],"all source frames reach native layer ABI");
                int duration=0;for(int delay:layer->delays)duration+=delay;
                Require(duration==durations[group][variant] && layer->z==-1 && layer->origin==body.p,"original timing, depth and body anchor");
                const int before=creates;
                ModernCapeEffects::SyncSequence(render,&graph,avatar,&overlay,sequence,1,action.first);
                Require(creates==before && layer->animations==1 && layer->flip==1,"turning preserves selected companion and animation clock");
                totalFrames+=layer->frames;++totalVariants;
            }
        }
    }
    Require(totalFrames==136 && totalVariants==16 && live==0,"136 real frames in all 16 original variant sequences, no leaked layers");
    for(int group=0;group<2;++group) {
        const int coat=group?1052439:1052408,shoes=group?1072648:1072647,id=group?153:152;
        AvatarState state;state.random.seed(239+group);
        auto tick=[&](const wchar_t* action,int flip=0){Update(state,catalog.definitions,avatar,manager,&graph,action,flip);};
        equip(coat,0);tick(L"stand1");Require(state.animations.empty(),"coat alone cannot trigger set");
        equip(0,shoes);tick(L"stand1");Require(state.animations.empty(),"shoes alone cannot trigger set");
        equip(coat,group?1072647:1072648);tick(L"stand1");Require(state.animations.empty(),"mixed schools cannot trigger set");
        equip(coat,shoes);tick(L"stand1");Require(state.animations.size()==1 && state.animations.at(id).render.layer.p,"matching pair triggers only its own companion");
        auto& animation=state.animations.at(id);auto first=animation.sequence;auto rng=state.random;const int before=creates;
        for(int i=0;i<20;++i)tick(L"stand1",i%2);
        Require(creates==before && animation.sequence==first && state.random==rng,"updates/facing never reroll or recreate standing animation");
        overlay.alpha=0x66ffffff;overlay.visible=0;tick(L"stand1");
        auto layer=static_cast<Layer*>(animation.render.layer.p);Require(layer->alpha==overlay.alpha && layer->visible==0,"avatar alpha/hide propagate");
        overlay.alpha=-1;overlay.visible=1;tick(L"stand1");layer=static_cast<Layer*>(animation.render.layer.p);
        Require(layer->visible==1,"avatar visibility restores");
        Require(animation.sequence==first && state.random==rng,"hide/unhide preserves random companion selection");
        ModernCapeEffects::HiddenByChair()=[](void*,int){return true;};tick(L"stand1");Require(layer->visible==0,"chair removeEffect hides companion");
        ModernCapeEffects::HiddenByChair()=nullptr;tick(L"stand1");Require(layer->visible==1,"standing restores companion");
        *reinterpret_cast<void**>(avatar+0x10b8)=nullptr;tick(L"stand1");Require(!animation.render.layer.p && !animation.render.failed,"missing body during look rebuild is recoverable");
        *reinterpret_cast<void**>(avatar+0x10b8)=body.p;tick(L"stand1");Require(animation.render.layer.p && animation.sequence==first,"rebuild preserves selection");
        for(auto action:{L"walk1",L"walk2",L"jump",L"ladder",L"rope",L"sit",L"prone",L"swingO1"}) {
            tick(action);Require(!animation.render.layer.p && animation.sequence.empty(),"unsupported action clears standing artwork");
            tick(L"stand1");Require(animation.render.layer.p,"standing after action restores complete companion");
        }
        std::set<std::wstring> seen;
        for(int i=0;i<128;++i){tick(L"walk1");tick(L"stand1");seen.insert(animation.sequence);}
        Require(seen.size()==4,"independent standing entries can select all four companions");
        tick(L"stand2");Require(animation.sequence.find(L"stand2/")==0,"two-handed stance uses original stand2 sequence");
        AvatarState other;other.random.seed(18);auto saved=animation.sequence;auto savedRng=state.random;
        Update(other,catalog.definitions,avatar,manager,&graph,L"stand1",1);
        Require(other.animations.at(id).render.layer.p!=animation.render.layer.p && animation.sequence==saved && state.random==savedRng,"other avatar owns independent layer and random state");
        other.animations.clear();equip(coat,0);tick(L"stand1");Require(state.animations.empty() && live==0,"unequip removes and releases companion");
    }
    // Exercise the exact resource-hook adapter. Keep every old set and its
    // order/objects: v83 matches sets using their enumeration index.
    Install(true);auto full=ModernCapeEffects::Resource(L"Effect/SetEff.img",manager);auto original=Query(full.v,PropertyIID());
    auto names=Names(original.p);Require(names.size()==103,"101 legacy sets plus two complete imported source sets");
    BSTR path=SysAllocString(L"/Data/Effect/SetEff.img");AdaptNativeRoot(factory,manager,path,&full.v);SysFreeString(path);
    auto legacy=Query(full.v,PropertyIID());auto oldNames=Names(legacy.p);
    Require(oldNames.size()==101,"native matcher excludes modern action sets");
    for(size_t i=0;i<oldNames.size();++i) {
        Require(oldNames[i]==names[i],"native set order unchanged");
        auto a=Resolve(original.p,oldNames[i]),b=Resolve(legacy.p,oldNames[i]);
        Require(MixedDyeResources::Unknown(a.v)==MixedDyeResources::Unknown(b.v),"legacy set resources unmodified");
    }
    for(int id:{152,153}) {
        auto a=Resolve(original.p,std::to_wstring(id)),b=Resolve(legacy.p,std::to_wstring(id));
        Require(Query(a.v,PropertyIID()).p && Empty(b.v),"source remains complete while legacy reader cannot misinterpret random structure");
    }
    equip(1052408,1072647);Tick(avatar,manager,&graph,L"stand1",0);
    Require(States().at(avatar).animations.at(152).render.layer.p,"production update callback works");
    ModernCapeEffects::OnSetEffectsDestroyed()(avatar);Require(States().empty() && live==0,"production avatar destruction releases companion");
    Install(false);
    std::puts("PASS school sets: 136 original frames, 16 sequences, pair conditions, four random choices, per-avatar state, action/facing/hide/rebuild/unequip/destruction and 101 unchanged native sets");
}
