#pragma once
#include <array>

namespace WardrobeLayout {
constexpr int Width=780,Height=524;
struct Bounds {int x,y,w,h;bool Contains(int px,int py)const{return px>=x&&py>=y&&px<x+w&&py<y+h;}};
constexpr Bounds Preview{20,94,296,320};
constexpr int PreviewArtX=42,PreviewArtY=102;
constexpr int PreviewWorldX=-376,PreviewWorldY=-260;
constexpr int RopeX=-229,RopeTop=-205,RopeBottom=-127;
constexpr Bounds Catalog{338,94,426,354};
constexpr Bounds Modal{257,166,266,192};
constexpr int GridX=352,GridY=126,GridColumns=11,GridRows=8,CellPitch=36,PageY=417;
constexpr int StatusY=462,FooterY=484;
constexpr std::array<const wchar_t*,8> Categories={L"全部",L"帽子",L"衣服",L"披风",L"武器",L"鞋子",L"手套",L"饰品"};
constexpr Bounds Category(int index){return {338+index*53,94,55,27};}
constexpr int CategoryId(int index){return 300+index;}
constexpr std::array<const wchar_t*,3> Scenes={L"商城",L"森林",L"天空"};
constexpr Bounds Scene(int index){return {20+index*74,66,74,26};}
}
