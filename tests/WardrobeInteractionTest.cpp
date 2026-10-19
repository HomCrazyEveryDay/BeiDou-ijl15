#include "../ezorsia/WardrobeModel.h"
#include "../ezorsia/WardrobeLayout.h"
#include <cassert>
#include <iostream>
#include <windows.h>
using namespace WardrobeModel;
enum class Dialog {None,Save,Rename,Delete,Binding,Overwrite,Details};
struct Control {int id,x,y,w,h;bool enabled;std::uint64_t uid;};
struct State {
    unsigned char* window=nullptr;
    Model model;bool pending=false,avatarDirty=false,shown=true,ready=true,deferredClose=false,dragging=false;
    int selectedSource=0,selected=0,applyingPreset=-1,preset=-1,dropdown=-1,page=0,pressed=-1,dragX=0,dragY=0,hover=-1;
    int detailSlot=0,saveSlot=0,menuPreset=-1,source=0,category=0,gridPage[3]{},edit=-1;
    std::uint64_t clickUid=0;std::vector<Control> controls;std::vector<Scope> saveScope;
    std::map<short,std::uint64_t> applyDraft;std::wstring status,search,outfitName,dialogMessage;Dialog dialog=Dialog::None;
} state;
static int sends=0,lastAction=-1,closes=0;static bool socketAvailable=true;
static std::vector<unsigned char> sent;
static const Item* Selected(){const auto& v=state.model.current.items[state.selectedSource];return state.selected>=0&&state.selected<int(v.size())?&v[state.selected]:nullptr;}
static void Invalidate(){}static void TooltipClear(){}static void Focus(bool){}static void Edit(int id){state.edit=id;}
static void CloseWindow(){++closes;state.shown=false;}
namespace WardrobePreview{void SetScene(int){}}
static bool Send(unsigned action,const std::vector<unsigned char>& bytes){
    if(!socketAvailable)return false;assert(!state.pending);++sends;lastAction=action;sent=bytes;state.pending=true;return true;
}
static void Fail(const char*){assert(false);}
static std::vector<int> drawnIcons;
static void Image(void*,const wchar_t*,int,int){}
static void Text(void*,const std::wstring&,int,int,unsigned=0,int=536){}
static void Rect(void*,int,int,int,int,unsigned){}
static void Icon(void*,int id,int,int){drawnIcons.push_back(id);}
static void Add(int id,int x,int y,int w,int h,bool enabled=true){state.controls.push_back({id,x,y,w,h,enabled,0});}
static void Field(void*,int,int,int,int,const std::wstring&,bool){}
static void DialogButton(void*,int id,int x,int y,bool,bool enabled=true){Add(id,x,y,40,20,enabled);}
static std::string ItemName(int id){return std::to_string(id);}
template<typename T>static T Method(void* p,unsigned slot){return reinterpret_cast<T>((*static_cast<void***>(p))[slot/sizeof(void*)]);}
struct RetainedLayer {void** vtable;int visible;};
static HRESULT __stdcall PutVisible(void* p,int visible){static_cast<RetainedLayer*>(p)->visible=visible;return S_OK;}
#include "WardrobeInteractionUnderTest.h"
static Item Make(std::uint64_t uid,int id,short slot,short pos=1,unsigned flags=3){Item i;i.uid=uid;i.id=id;i.slot=slot;i.position=pos;i.flags=flags;return i;}
static void Reset(){state=State{};sends=closes=0;sent.clear();socketAvailable=true;lastAction=-1;}
static void DrawCell(int id){const auto& item=state.model.current.items[(id-1000)/1024][(id-1000)%1024];state.controls={{id,id,150,32,32,true,item.uid}};}
static void DoubleClick(int hit){DrawCell(hit);Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,hit,150);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,hit,150);Mouse(nullptr,nullptr,WM_LBUTTONDBLCLK,0,hit,150);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,hit,150);}
int main(){
    {
        Reset();void* vtable[0x120/sizeof(void*)]{};vtable[0x11c/sizeof(void*)]=reinterpret_cast<void*>(PutVisible);
        RetainedLayer layers[3]{{vtable,1},{vtable,1},{vtable,1}};alignas(void*) unsigned char window[0x24]{};
        state.window=window;
        for(int i=0;i<3;++i)*reinterpret_cast<void**>(window+0x18+i*4)=&layers[i];
        HideWindowLayers();for(const auto& layer:layers)assert(layer.visible==0);
        // A quarantined preview retaining a layer cannot keep the closed window visible.
        state.ready=false;layers[0].visible=1;HideWindowLayers();assert(layers[0].visible==1);
    }
    Reset();state.model.current.items[0]={Make(11,1000000,-101)};
    DoubleClick(1000);assert(state.model.Desired(-101)==11&&sends==0);
    DoubleClick(1000);assert(!state.model.Desired(-101)&&sends==0);
    state.page=2;DoubleClick(1000);assert(sends==1&&lastAction==Take&&sent.size()==8&&sent[0]==11);
    DoubleClick(1000);assert(sends==1); // Pending double-click cannot re-send or toggle anything.
    Reset();state.page=2;state.model.current.items[1]={Make(21,1000000,-101)};
    DoubleClick(2024);assert(sends==1&&lastAction==Store&&sent[0]==21);
    Reset();state.model.current.items[0]={Make(31,1000000,-101)};state.model.current.items[2]={Make(30,1000001,-101,-101)};
    Outfit saved;saved.defined=true;saved.entries=state.model.SaveScope();for(auto& e:saved.entries)if(e.slot==-101){e.uid=31;e.itemId=1000000;}
    state.model.current.outfits[2]=saved;
    state.model.original=true; // Entering preset page shows actual wear without disabling APPLY.
    LoadPreset(2);assert(sends==1&&lastAction==Apply&&state.applyingPreset==2&&state.preset==-1);
    assert(state.model.draft.empty()&&state.model.Base(-101)==30&&state.dialog==Dialog::None);
    Reader payload{sent.data(),sent.data()+sent.size()};assert(payload.number(1)==0&&payload.number(1)==1&&short(payload.number(2))==-101&&payload.number(8)==31);
    LoadPreset(2);assert(sends==1); // One network apply per click, pending blocks repeats.
    state.pending=false;state.model.current.items[0][0].flags|=8;sends=0;
    LoadPreset(2);assert(sends==0&&state.dialog==Dialog::Binding&&state.model.draft.empty());
    ApplyDraft(true);assert(sends==1&&sent[0]==1);
    state.pending=false;state.dialog=Dialog::None;state.model.current.items[0].clear();sends=0;
    LoadPreset(2);assert(sends==0&&state.preset==-1&&!state.status.empty()); // Missing-only preset explains why nothing can change.
    state.model.current.items[0]={Make(31,1000000,-101)};socketAvailable=false;
    LoadPreset(2);assert(sends==0&&state.applyingPreset==-1&&state.applyDraft.empty());
    Reset();state.model.current.items[0]={Make(41,1000000,-101)};state.model.current.items[2]={Make(40,1000001,-101,-101)};
    Outfit current;current.defined=true;current.entries=state.model.SaveScope();state.model.current.outfits[3]=current;
    assert(state.model.Toggle(41));state.model.original=true;
    LoadPreset(3);assert(sends==0&&state.preset==3&&state.model.draft.at(-101)==41);
    for(auto& e:state.model.current.outfits[3].entries){e.mode=0;e.uid=0;}
    state.preset=-1;LoadPreset(3);assert(sends==1&&state.preset==-1&&state.applyDraft.at(-101)==0); // Legacy KEEP now means empty for an editable slot.
    Reset();state.page=2;state.model.current.items[0]={Make(51,1000000,-101)};DrawCell(1000);
    Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,1000,150);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,1000,150);
    state.model.current.items[0][0].uid=52;assert(Hit(1000,150)==-1); // Old painted cell cannot address the new list occupant.
    DrawCell(1000);Mouse(nullptr,nullptr,WM_LBUTTONDBLCLK,0,1000,150);assert(sends==0); // Even after redraw, don't complete an old item's double-click on a new UID.
    DoubleClick(1000);assert(sends==1&&sent[0]==52);
    Reset();state.model.current.outfits[0].defined=true;BeginSave(0);state.outfitName=L"\u2603";
    Activate(401);assert(state.dialog==Dialog::Save&&!state.dialogMessage.empty()&&sends==0);
    state.outfitName=L"Look1";Activate(401);assert(state.dialog==Dialog::Overwrite&&sends==0);
    Activate(400);assert(state.dialog==Dialog::Save&&state.edit==450);
    Activate(401);Activate(402);assert(sends==1&&lastAction==Save&&state.dialog==Dialog::None);
    Activate(4);assert(state.page==0); // Pending requests block page switches in actual routing.
    Activate(1);assert(closes==1);
    Reset();state.model.current.items[0]={Make(61,1000000,-101)};assert(state.model.Toggle(61));
    Activate(4);assert(state.page==1&&state.model.original&&state.model.draft.at(-101)==61);
    Activate(3);assert(state.page==0&&!state.model.original&&state.model.draft.at(-101)==61);
    state.model.draft[-101]=999;Activate(4);Activate(105);assert(state.dialog==Dialog::Save&&state.saveSlot==5); // Hidden invalid preview does not block saving actual wear.
    Activate(400);assert(state.dialog==Dialog::None);
    state.menuPreset=2;Activate(13);Activate(402);assert(sends==1&&lastAction==Delete&&sent[0]==3);
    Reset();state.model.current.items[0]={Make(91,1000000,-101)};
    state.model.current.items[2]={Make(90,1100000,-109,-109)};
    Outfit legacy;legacy.defined=true;legacy.entries=state.model.SaveScope();
    for(auto& e:legacy.entries){if(e.slot==-101)e={e.slot,2,900,1000000,false};if(e.slot==-109)e={e.slot,2,901,1109999,false};}
    state.model.current.outfits[1]=legacy;state.page=1;state.model.original=true;
    Activate(101);assert(sends==0&&state.applyingPreset==-1&&!state.status.empty());
    assert(state.model.Base(-109)==90&&state.preset==-1); // Missing cape blocks the full outfit; no silent mixing.
    Reset();state.page=1;state.model.original=true;
    state.model.current.items[2]={Make(101,1002186,-101,-101),Make(102,1050000,-105,-105),Make(103,1070000,-107,-107),Make(104,1080000,-108,-108),Make(105,1100000,-109,-109)};
    Outfit five;five.defined=true;five.entries=state.model.SaveScope();state.model.current.outfits[0]=five;state.preset=0;
    Outfit one=five;for(auto& e:one.entries)if(e.slot!=-101)e={e.slot,0,0,0,true}; // Reproduce old stored KEEP fields.
    state.model.current.outfits[1]=one;Activate(101);
    assert(sends==1&&lastAction==Apply&&state.applyingPreset==1&&state.preset==0);
    Reader full{sent.data(),sent.data()+sent.size()};assert(full.number(1)==0&&full.number(1)==4);
    std::map<short,std::uint64_t> removals;
    for(int i=0;i<4;i++){short slot=short(full.number(2));removals[slot]=full.number(8);}
    assert(removals==state.applyDraft&&full.p==full.end);
    for(short slot:{short(-105),short(-107),short(-108),short(-109)})assert(removals.at(slot)==0);
    assert(state.model.current.items[2].size()==5); // Click waits for server success, never mutates inventory locally.
    Reset();state.model.current.items[1]={Make(190,1000000,-101)};
    assert(state.model.Toggle(190));BeginSave();assert(sends==0&&state.dialog==Dialog::Save);Popup(Dialog::None);
    Outfit bagOnly;bagOnly.defined=true;bagOnly.entries=state.model.SaveScope();state.model.current.outfits[0]=bagOnly;
    state.model.current.freeBag=0;state.status.clear();LoadPreset(0);
    assert(sends==1&&lastAction==Apply&&state.applyingPreset==0); // Bag items participate directly, with no storage prerequisite.
    Reset();state.page=2;state.model.current.capacity=240;
    state.model.current.items[1]={Make(201,1000000,-101)};state.model.current.items[0]={Make(202,1000001,-101)};
    state.selectedSource=1;Activate(21);assert(sends==0);Activate(20);assert(sends==1&&lastAction==Store&&sent[0]==201);
    Activate(22);assert(sends==1); // Pending single transfer cannot start a batch.
    state.pending=false;state.selectedSource=0;Activate(20);assert(sends==1);Activate(21);assert(sends==2&&lastAction==Take&&sent[0]==202);
    state.pending=false;state.search=L"hidden";state.category=7;
    state.model.current.items[1].push_back(Make(203,1100000,-109,2,1));
    state.model.current.items[2]={Make(204,1070000,-107,-107)};
    Activate(22);assert(sends==3&&lastAction==Store&&sent.size()==8);
    for(auto byte:sent)assert(byte==0); // One explicit batch request, independent of search/category/selection.
    state.pending=false;state.model.current.capacity=1;Activate(22);assert(sends==3&&!state.status.empty()); // Cannot fit all: no partial send.
    state.model.current.items[1][0].flags=1;state.model.current.capacity=240;Activate(22);assert(sends==3&&!state.status.empty());
    state.model.current.items[1][0].flags=3;state.page=0;Activate(22);assert(sends==3); // Storage buttons never mutate from preview page.
    state.page=2;state.selectedSource=1;socketAvailable=false;state.status.clear();
    Activate(20);assert(sends==3&&!state.status.empty());state.status.clear();
    Activate(22);assert(sends==3&&!state.status.empty());
    Reset();state.page=1;Outfit detailed;detailed.defined=true;detailed.name="Full outfit";
    for(int i=0;i<int(Slots.size());i++)detailed.entries.push_back({Slots[i],2,std::uint64_t(400+i),1000000+i,true});
    state.model.current.outfits[2]=detailed;Activate(212);assert(state.dialog==Dialog::Details&&state.detailSlot==2&&sends==0);
    DrawDialog(nullptr);assert(drawnIcons.size()==15&&state.controls.size()==16);
    const auto cell=state.controls[0];
    Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,cell.x,cell.y);Mouse(nullptr,nullptr,WM_LBUTTONDBLCLK,0,cell.x,cell.y);
    assert(sends==0&&state.model.draft.empty()); // Detail cells never select/apply a catalog item.
    Activate(400);assert(state.dialog==Dialog::None&&sends==0);
    std::cout<<"PASS actual Mouse/Hit/Activate/Save callbacks, complete 15-slot details and read-only detail interactions\n";
}
