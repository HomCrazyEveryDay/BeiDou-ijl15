#pragma once
#include <array>

namespace WardrobeLayout {
constexpr int Width=680,Height=462;
struct Bounds {int x,y,w,h;bool Contains(int px,int py)const{return px>=x&&py>=y&&px<x+w&&py<y+h;}};
// Native CashShop preview bezel; the 212x165 shop scene fills its viewport 1:1.
constexpr int BezelX=12,BezelY=76;
constexpr Bounds Preview{20,84,212,165};
constexpr int PreviewWorldX=-376,PreviewWorldY=-260;
constexpr int RightX=254,FooterY=434,CellPitch=35;
constexpr int GridX=265,GridY=107,GridColumns=11,GridRows=8;
constexpr int StripX=15,StripY=285,StripColumns=6;
constexpr int OutfitX=12,OutfitY=282;
constexpr int SlotWellX=266,SlotWellY=80,SlotWellW=388,SlotWellH=316;
constexpr int StorageY=81,StorageRows=9,BagX=15,BagColumns=7,LockerX=350,LockerColumns=9;
constexpr Bounds Modal{207,135,266,192};
constexpr std::array<const wchar_t*,8> Categories={L"全部",L"帽子",L"衣服",L"披风",L"武器",L"鞋子",L"手套",L"饰品"};
constexpr Bounds CategoryTab(int index){return {RightX+index*51,80,49,19};}
constexpr int CategoryId(int index){return 300+index;}
constexpr std::array<const wchar_t*,3> Scenes={L"商城",L"森林",L"天空"};
constexpr Bounds Scene(int index){return {116+index*42,56,40,19};}
constexpr Bounds PageTab(int index){return {12+index*58,30,56,19};}
constexpr Bounds OutfitCard(int index){return {OutfitX+index%2*116,OutfitY+index/2*38,112,34};}
// Selected-outfit slot cards, 5x3 in body order: head row, body row, shield and rings.
constexpr int SlotOrder[]={0,1,2,3,8,4,5,6,7,10,9,11,12,13,14};
constexpr Bounds SlotCard(int position){return {SlotWellX+6+position%5*76,SlotWellY+6+position/5*104,72,96};}}
