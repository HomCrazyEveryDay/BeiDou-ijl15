#pragma once
#include "MonthlyShopModel.h"
#include <algorithm>

// Matches the native CShopDlg buy CCtrlTab: x=5, y=95, width=222, height=21.
// The native control still owns hit testing; input arrives in control-local pixels.
namespace MonthlyShopTabs {
constexpr int Left=5,Top=95,Width=222,Height=21;
constexpr int All=-1,Previous=-2,Next=-3,None=-4;
struct Cell { int left,width,id; };
inline std::vector<int> Months(const MonthlyShopModel::Snapshot& snapshot){
    std::vector<int> out;for(const auto& month:snapshot.months)if(month.id>0)out.push_back(month.id);return out;
}
inline int Slots(int count){return count>3?2:3;}
inline int LastPage(int count){return count?(count-1)/Slots(count):0;}
inline int SelectedPage(const MonthlyShopModel::Snapshot& snapshot){
    auto months=Months(snapshot);
    for(size_t i=0;i<months.size();++i)if(months[i]==snapshot.month)return static_cast<int>(i)/Slots(static_cast<int>(months.size()));
    return 0;
}
inline std::vector<Cell> Cells(const MonthlyShopModel::Snapshot& snapshot,int page){
    auto months=Months(snapshot);const int count=static_cast<int>(months.size()),slots=Slots(count);
    page=(std::max)(0,(std::min)(page,LastPage(count)));
    std::vector<Cell> cells{{0,42,All}};
    const int arrow=count>3?24:0,left=42+arrow,width=(Width-42-2*arrow)/slots;
    if(arrow)cells.push_back({42,arrow,Previous});
    for(int i=0;i<slots && page*slots+i<count;++i)cells.push_back({left+i*width,width,months[page*slots+i]});
    if(arrow)cells.push_back({Width-arrow,arrow,Next});
    return cells;
}
inline int Hit(const MonthlyShopModel::Snapshot& snapshot,int page,int x,int y){
    if(y<0 || y>=Height)return None;
    for(const auto& cell:Cells(snapshot,page))if(x>=cell.left && x<cell.left+cell.width)return cell.id;
    return None;
}
inline bool Selectable(const MonthlyShopModel::Snapshot& snapshot,int id,bool pending){
    if(pending || id==snapshot.month)return false;
    for(const auto& month:snapshot.months)if(month.id==id)return true;
    return false;
}
}
