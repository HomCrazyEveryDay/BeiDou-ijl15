#pragma once
#include "ModernCapeEffects.h"
#include <array>

namespace ModernChairResources {
using namespace MixedDyeResources;
using ModernCapeEffects::Number;
struct Seat {
    int x=0,y=0,grade=0,reverse=0,minGap=5000,maxGap=10000;
    bool present=false;
    std::vector<std::pair<std::wstring,int>> actions;
};
struct Settings {
    bool weapon=false,cape=false,effect=false;
    std::array<Seat,3> seats;
};
inline Settings Read(int id,void* manager=nullptr) {
    Settings result;if(id/10000!=301)return result;
    wchar_t path[96];swprintf_s(path,L"Item/Install/0301.img/%08d/info",id);
    auto value=ModernCapeEffects::Resource(path,manager);auto info=Query(value.v,PropertyIID());
    if(!info.p)return result;
    result.weapon=Number(info.p,L"invisibleWeapon",0)!=0;
    result.cape=Number(info.p,L"invisibleCape",0)!=0;
    result.effect=Number(info.p,L"removeEffect",0)!=0;
    auto customValue=Resolve(info.p,L"customChair");auto custom=Query(customValue.v,PropertyIID());
    if(!custom.p)return result;
    auto type=Resolve(custom.p,L"type");
    if(type.v.vt!=VT_BSTR || !type.v.bstrVal || wcscmp(type.v.bstrVal,L"petChair"))return result;
    auto seatsValue=Resolve(custom.p,L"petChairInfo");auto seats=Query(seatsValue.v,PropertyIID());
    if(!seats.p)return result;
    for(int i=0;i<3;++i) {
        auto seatValue=Resolve(seats.p,std::to_wstring(i));auto property=Query(seatValue.v,PropertyIID());
        if(!property.p)continue;
        auto position=Resolve(property.p,L"pos");auto vector=Query(position.v,VectorIID());if(!vector.p)continue;
        Seat seat;seat.x=Int(vector.p,0x20);seat.y=Int(vector.p,0x28);
        if(abs(seat.x)>2048 || abs(seat.y)>2048)continue;
        seat.grade=Number(property.p,L"wonderGrade",0);seat.reverse=Number(property.p,L"reverseFlip",0)!=0;
        auto randomValue=Resolve(property.p,L"randAct");auto random=Query(randomValue.v,PropertyIID());
        if(random.p) {
            seat.minGap=(std::max)(1000,(std::min)(60000,Number(random.p,L"tGapMin",5000)));
            seat.maxGap=(std::max)(seat.minGap,(std::min)(60000,Number(random.p,L"tGapMax",10000)));
            for(auto& name:Names(random.p)) {
                auto actionValue=Resolve(random.p,name);auto action=Query(actionValue.v,PropertyIID());
                if(!action.p)continue;
                int probability=Number(action.p,L"prob",0);
                if(probability>0 && probability<=10000 && seat.actions.size()<24)seat.actions.emplace_back(name,probability);
            }
        }
        seat.present=true;result.seats[i]=std::move(seat);
    }
    return result;
}
inline bool PetFits(int id,const Seat& seat,void* manager=nullptr) {
    if(!seat.present || id/10000!=500)return false;
    if(!seat.grade)return true;
    wchar_t path[80];swprintf_s(path,L"Item/Pet/%d.img/info",id);
    auto value=ModernCapeEffects::Resource(path,manager);auto info=Query(value.v,PropertyIID());
    return info.p && Number(info.p,L"wonderGrade",0)==seat.grade;
}
// Only the visible weapon/cape in the native AvatarLook are filtered while
// composing a chair pose. The live equipment and its cached look are restored.
template<class Refresh> inline void RefreshLook(void* avatar,const Settings& settings,int force,Refresh refresh) {
    auto bytes=static_cast<BYTE*>(avatar);
    const int offsets[]={0x49,0x41};int saved[2]{};bool hidden[]={settings.weapon,settings.cape};
    for(int i=0;i<2;++i)if(hidden[i]){std::memcpy(&saved[i],bytes+offsets[i],4);std::memset(bytes+offsets[i],0,4);}
    struct Restore {
        BYTE* bytes;const int* offsets;int* saved;bool* hidden;
        ~Restore(){for(int i=0;i<2;++i)if(hidden[i]){std::memcpy(bytes+offsets[i],&saved[i],4);std::memcpy(bytes+0x1c5+offsets[i],&saved[i],4);}}
    } restore{bytes,offsets,saved,hidden};
    refresh(avatar,force);
}
}
