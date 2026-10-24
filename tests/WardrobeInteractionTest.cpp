#include "../ezorsia/WardrobeModel.h"
#include "../ezorsia/WardrobeLayout.h"
#include <cassert>
#include <iostream>
#include <windows.h>
using namespace WardrobeModel;
using WardrobeLayout::Width;
using WardrobeLayout::Height;
namespace Client {int m_nGameWidth=1024,m_nGameHeight=768;}
enum class Dialog {None,Save,Rename,Delete,Binding,Overwrite};
struct Control {int id,x,y,w,h;bool enabled;std::uint64_t uid;};
struct State {
    unsigned char* window=nullptr;
    Model model;bool pending=false,avatarDirty=false,shown=true,ready=true,deferredClose=false,dragging=false;
    int selectedSource=0,selected=0,applyingPreset=-1,preset=-1,dropdown=-1,page=0,pressed=-1,dragX=0,dragY=0,hover=-1,soundHover=-1;
    int saveSlot=0,menuPreset=-1,source=3,category=0,gridPage[4]{},edit=-1,outfit=0,pendingAction=0,windowX=0,windowY=0;
    std::uint64_t clickUid=0;std::vector<Control> controls;std::vector<Scope> saveScope;
    std::map<short,std::uint64_t> applyDraft;std::wstring status,search,outfitName,dialogMessage;Dialog dialog=Dialog::None;
} state;
static int sends=0,lastAction=-1,closes=0;static bool socketAvailable=true;
static std::vector<unsigned char> sent;
static const Item* Selected(){const auto& v=state.model.current.items[state.selectedSource];return state.selected>=0&&state.selected<int(v.size())?&v[state.selected]:nullptr;}
static void Invalidate(){}static void TooltipClear(){}static void Focus(bool){}static void Edit(int id){state.edit=id;}
static void CloseWindow(){++closes;state.shown=false;}
static void Cursor(int){}
static const Item* tooltipItem=nullptr;
static void ShowItemTooltip(const Item& item,int,int){tooltipItem=&item;}
namespace WardrobePreview{int scene=0;int Scene(){return scene;}void SetScene(int value){scene=value;}}
#include "WardrobeNativeSoundHarness.h"
static bool Send(unsigned action,const std::vector<unsigned char>& bytes){
    if(!socketAvailable)return false;assert(!state.pending);++sends;lastAction=action;sent=bytes;state.pending=true;return true;
}
static void Fail(const char*){assert(false);}
static void Check(HRESULT result){assert(SUCCEEDED(result));}
static std::vector<int> drawnIcons;
static void Image(void*,const wchar_t*,int,int){}
static void Text(void*,const std::wstring&,int,int,unsigned=0,bool=false){}
static std::vector<std::wstring> drawnLines;
#include "WardrobeWrapUnderTest.h"
static int TextLines(void*,const std::wstring& s,int,int,int width,unsigned=0,bool=false,int=13){auto lines=Wrap(s,width);drawnLines.insert(drawnLines.end(),lines.begin(),lines.end());return int(lines.size());}
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
static void Reset(){state=State{};sends=closes=0;sent.clear();sounds.clear();socketAvailable=true;lastAction=-1;tooltipItem=nullptr;}
static void DrawCell(int id){const auto& item=state.model.current.items[(id-1000)/1024][(id-1000)%1024];state.controls={{id,id,150,32,32,true,item.uid}};}
static void DoubleClick(int hit){DrawCell(hit);Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,hit,150);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,hit,150);Mouse(nullptr,nullptr,WM_LBUTTONDBLCLK,0,hit,150);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,hit,150);}
int wmain(int argc,wchar_t** argv){
    assert(argc==2);InstallNativeSoundHarness(argv[1]);
    CheckNativeSoundEncoding();
    // Sorting is one server transaction, independent of filters, and never rewrites a local item index.
    for(int page:{0,2}){
        Reset();state.page=page;state.search=L"hidden";state.category=7;
        state.model.current.items[0]={Make(811,1102000,-109),Make(812,1002000,-101)};
        state.model.current.items[1]={Make(813,1001000,-101)};assert(state.model.Toggle(812));
        const auto draft=state.model.draft;Activate(27);
        assert(sends==1&&lastAction==Sort&&sent.empty()&&state.model.current.items[0][0].uid==811&&state.model.draft==draft);
        Activate(27);assert(sends==1); // Pending click cannot enqueue another sort.
        state.pending=false;socketAvailable=false;Activate(27);assert(sends==1&&!state.status.empty());
    }
    Reset();Activate(27);assert(sends==0);
    state.model.current.items[0]={Make(811,1102000,-109)};Activate(27);assert(sends==0);
    state.model.current.items[0].push_back(Make(812,1002000,-101));state.page=1;Activate(27);assert(sends==0);
    // All-source sorting groups IDs across locations without mutating bag/worn order or duplicate identities.
    Reset();state.model.current.items[0]={Make(821,1102000,-109),Make(822,1002000,-101)};
    state.model.current.items[1]={Make(823,1042000,-105),Make(824,1002000,-101)};
    state.model.current.items[2]={Make(825,1002000,-101,-101),Make(826,1072000,-107,-107)};
    {const auto entries=Entries(AllSources);std::vector<std::uint64_t> ids;
        for(auto entry:entries)ids.push_back(state.model.current.items[entry.first][entry.second].uid);
        assert((ids==std::vector<std::uint64_t>{825,824,822,823,826,821}));
        assert(Entries(0)[0].second==0&&Entries(1)[0].second==0);
        assert(state.model.Toggle(822)&&Entries(AllSources)==entries); // Try-on cannot move the clicked cells.
    }
    Reset();state.pending=true;state.pendingAction=Sort;Snapshot sorted;sorted.outcome=Sort;
    ReplySound(sorted);assert(sounds.size()==1&&sounds[0]==L"DragEnd");
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
    assert(sounds.back()==L"DragEnd");
    DoubleClick(1000);assert(!state.model.Desired(-101)&&sends==0);
    state.page=2;DoubleClick(1000);assert(sends==1&&lastAction==Take&&sent.size()==8&&sent[0]==11);
    DoubleClick(1000);assert(sends==1); // Pending double-click cannot re-send or toggle anything.
    const auto pendingSounds=sounds.size();DoubleClick(1000);assert(sounds.size()==pendingSounds);
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
    state.model.draft[-101]=999;Activate(4);Activate(105);assert(state.outfit==5&&state.dialog==Dialog::None&&sends==0); // Selecting only previews.
    Activate(25);assert(state.dialog==Dialog::Save&&state.saveSlot==5); // Hidden invalid preview does not block saving actual wear.
    Activate(400);assert(state.dialog==Dialog::None);
    Activate(13);assert(state.dialog==Dialog::None); // Empty position cannot be deleted.
    state.model.current.outfits[2].defined=true;Activate(102);Activate(13);Activate(402);assert(sends==1&&lastAction==Delete&&sent[0]==3);
    Reset();state.model.current.items[0]={Make(91,1000000,-101)};
    state.model.current.items[2]={Make(90,1100000,-109,-109)};
    Outfit legacy;legacy.defined=true;legacy.entries=state.model.SaveScope();
    for(auto& e:legacy.entries){if(e.slot==-101)e={e.slot,2,900,1000000,false};if(e.slot==-109)e={e.slot,2,901,1109999,false};}
    state.model.current.outfits[1]=legacy;state.page=1;state.model.original=true;
    Activate(101);assert(sends==0&&state.outfit==1);Activate(26);assert(sends==0&&state.applyingPreset==-1&&!state.status.empty());
    assert(state.model.Base(-109)==90&&state.preset==-1); // Missing cape blocks the full outfit; no silent mixing.
    Reset();state.page=1;state.model.original=true;
    state.model.current.items[2]={Make(101,1002186,-101,-101),Make(102,1050000,-105,-105),Make(103,1070000,-107,-107),Make(104,1080000,-108,-108),Make(105,1100000,-109,-109)};
    Outfit five;five.defined=true;five.entries=state.model.SaveScope();state.model.current.outfits[0]=five;state.preset=0;
    Outfit one=five;for(auto& e:one.entries)if(e.slot!=-101)e={e.slot,0,0,0,true}; // Reproduce old stored KEEP fields.
    state.model.current.outfits[1]=one;Activate(101);Activate(26);
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
    // Wear the try-on directly: one APPLY with exactly the changed slots.
    Reset();state.model.current.items[0]={Make(301,1000000,-101)};state.model.current.items[2]={Make(300,1000001,-101,-101)};
    assert(state.model.Toggle(301));Activate(24);
    assert(sends==1&&lastAction==Apply&&state.applyingPreset==6&&state.applyDraft==state.model.draft);
    Reader tryOn{sent.data(),sent.data()+sent.size()};assert(tryOn.number(1)==0&&tryOn.number(1)==1&&short(tryOn.number(2))==-101&&tryOn.number(8)==301);
    Reset();state.model.current.items[0]={Make(302,1000000,-101)};state.model.current.items[0][0].flags|=8;
    assert(state.model.Toggle(302));Activate(24);assert(sends==0&&state.dialog==Dialog::Binding);
    ApplyDraft(true);assert(sends==1&&sent[0]==1);
    // Strip cells undo one slot; restore clears all; overall/pants conflicts are refused, not mixed.
    Reset();state.model.current.items[0]={Make(311,1000000,-101),Make(322,1050000,-105)};
    state.model.current.items[2]={Make(320,1040000,-105,-105),Make(321,1060000,-106,-106)};
    assert(state.model.Toggle(311)&&state.model.Toggle(322)&&state.model.draft.size()==3);
    assert(PantsBlocked());
    Activate(5105);assert(state.model.draft.size()==3&&!state.status.empty()); // Worn pants cannot return under an overall.
    assert(sounds.back()==L"DlgNotice");
    state.hover=5104;Activate(5104);assert(state.model.draft.size()==1&&state.model.Desired(-106)==321&&state.model.Valid()&&state.hover==-1); // Next hover rebuilds the changed cell tooltip.
    assert(!PantsBlocked()&&sounds.back()==L"DragEnd");
    Activate(5100);assert(state.model.draft.empty()&&sends==0);
    assert(state.model.Toggle(311));Activate(23);assert(state.model.draft.empty()&&sends==0);
    state.page=1;assert(state.model.Toggle(311));Activate(5100);assert(state.model.draft.size()==1); // Only the wardrobe page edits the try-on.
    // "All" source: the worn piece replaced by a try-on stays listed; double-clicking it restores it.
    Reset();state.model.current.items[0]={Make(341,1000000,-101)};state.model.current.items[2]={Make(340,1000001,-101,-101)};
    Activate(7);assert(state.dropdown==7);Activate(40);assert(state.source==0&&state.dropdown==-1);Activate(43);assert(state.source==3);
    Activate(37);assert(state.gridPage[3]==1);Activate(36);assert(state.gridPage[3]==0);
    DoubleClick(1000);assert(state.model.Desired(-101)==341&&state.model.draft.size()==1);
    DoubleClick(1000+2*1024);assert(state.model.Desired(-101)==340&&state.model.draft.empty()&&sends==0);
    // Worn pet equipment / pet skill items never enter the catalog; unwearable fashion explains why.
    Reset();state.model.current.items[2]={Make(350,1000000,-101,-101),Make(351,1802896,-114,-114,0),Make(352,1812000,-121,-121,0)};
    {auto worn=Filter(2);assert(worn.size()==1&&worn[0]==0);}
    state.model.current.items[0]={Make(353,1000001,-101,1,2)};state.model.current.items[0][0].reason=2;
    DoubleClick(1000);assert(state.model.draft.empty()&&state.status.find(L"\u9650\u65f6")!=std::wstring::npos);
    assert(sounds.back()==L"DlgNotice");
    // Outfit page: click selects/previews, double-click wears.
    Reset();state.page=1;state.model.original=true;
    state.model.current.items[0]={Make(331,1000000,-101)};state.model.current.items[2]={Make(330,1000001,-101,-101)};
    Outfit wear;wear.defined=true;wear.name="Look";wear.entries=state.model.SaveScope();
    for(auto& e:wear.entries)if(e.slot==-101){e.mode=2;e.uid=331;e.itemId=1000000;}
    state.model.current.outfits[4]=wear;state.controls={{104,10,300,112,34,true,0}};
    Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,20,310);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,20,310);assert(state.outfit==4&&sends==0);
    Mouse(nullptr,nullptr,WM_LBUTTONDBLCLK,0,20,310);assert(sends==1&&lastAction==Apply&&state.applyingPreset==4);
    // Long binding lists wrap to two lines and are summarised, never cut with an ellipsis.
    Reset();
    for(int i=0;i<8;i++){Item item=Make(400+i,1000000+i,-101);item.flags|=8;item.name="ABCDEFGHIJKL";state.model.current.items[0].push_back(item);state.applyDraft[short(-101-i)]=400+i;}
    auto names=BindingNames(230);auto lines=Wrap(names,230);
    assert(lines.size()<=2&&names.find(L"\u7b49 8 \u4ef6")!=std::wstring::npos&&names.find(L"\u2026")==std::wstring::npos);
    // Pants availability follows the selected preview, not a hidden try-on or another outfit.
    Reset();state.model.current.items[2]={Make(501,1050000,-105,-105)};
    assert(PantsBlocked());
    Outfit overall;overall.defined=true;overall.entries=state.model.SaveScope();state.model.current.outfits[0]=overall;
    Outfit empty=overall;for(auto& e:empty.entries)e={e.slot,1,0,0,true};state.model.current.outfits[1]=empty;
    state.page=1;state.model.original=true;assert(PantsBlocked());
    SelectOutfit(1);assert(!PantsBlocked());SelectOutfit(0);assert(PantsBlocked());
    state.model.current.items[2].clear();assert(!PantsBlocked()); // Missing overall reserves no visible pants slot.
    // A pending operation only plays success after the server confirms that action.
    Reset();Snapshot reply;reply.outcome=Apply;ReplySound(reply);assert(sounds.empty());
    state.pending=true;state.pendingAction=Apply;ReplySound(reply);assert(sounds.back()==L"DragEnd");
    state.pending=false;auto heard=sounds.size();ReplySound(reply);assert(sounds.size()==heard); // Replay/refresh stays silent.
    state.pending=true;reply.outcome=Open;ReplySound(reply);assert(sounds.back()==L"DlgNotice"); // Rejection cannot sound like success.
    state.pendingAction=Open;ReplySound(reply);assert(sounds.back()==L"MenuUp");
    reply.busy=true;ReplySound(reply);assert(sounds.back()==L"DlgNotice");
    Reset();Activate(300);assert(sounds.size()==1&&sounds.back()==L"BtMouseClick");Activate(301);assert(sounds.size()==2&&sounds.back()==L"BtMouseClick");
    Activate(301);assert(sounds.size()==3); // Every intentional click sounds, including the selected tab.
    // Actual MouseMove -> Hit -> Hover: redraw/tooltip resets are not pointer transitions.
    Reset();Add(301,300,80,49,19);Add(302,350,80,49,19);Add(303,400,80,49,19);
    MouseMove(nullptr,nullptr,300,80);assert(sounds.size()==1&&sounds.back()==L"BtMouseOver");
    Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,300,80);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,300,80);
    assert(state.hover==-1&&sounds.size()==2&&sounds.back()==L"BtMouseClick");
    MouseMove(nullptr,nullptr,301,80);assert(sounds.size()==2);
    MouseMove(nullptr,nullptr,350,80);assert(sounds.size()==3&&sounds.back()==L"BtMouseOver");
    state.controls.clear();ResetSelection();MouseMove(nullptr,nullptr,351,80);assert(sounds.size()==3);
    Add(302,350,80,49,19);MouseMove(nullptr,nullptr,352,80);assert(sounds.size()==3); // Controls rebuilt under stationary pointer.
    state.pending=true;MouseMove(nullptr,nullptr,353,80);assert(sounds.size()==3);
    state.pending=false;MouseMove(nullptr,nullptr,354,80);assert(sounds.size()==4); // Re-enabled native button may sound once.
    MouseEnter(nullptr,nullptr,0);MouseMove(nullptr,nullptr,354,80);assert(sounds.size()==5);
    // All button families provide hover and immediate click feedback through the real mouse path.
    std::vector<int> buttonIds={1,3,4,7,9,12,13,18,19,20,21,22,23,24,25,26,400,401,402};
    for(auto range:{std::pair<int,int>{30,37},{40,43},{80,82},{100,105},{300,307},{460,465}})
        for(int id=range.first;id<=range.second;++id)buttonIds.push_back(id);
    for(int id:buttonIds){
        Reset();state.model.current.capacity=240;
        state.model.current.items[0]={Make(801,1000000,-101)};state.model.current.items[1]={Make(802,1000001,-101)};
        state.model.current.items[2]={Make(803,1000002,-101,-101)};
        Outfit saved;saved.defined=true;saved.name="Look";saved.entries=state.model.SaveScope();
        for(auto& e:saved.entries)if(e.slot==-101){e.uid=801;e.itemId=1000000;}
        for(auto& o:state.model.current.outfits)o=saved;
        state.saveScope=saved.entries;state.outfitName=L"Look";state.menuPreset=0;
        if(id==12 || id==13 || id==25 || id==26 || id>=100&&id<106)state.page=1;
        if(id==20 || id==21 || id==22){state.page=2;state.selectedSource=id==21?0:1;}
        if(id==23 || id==24)assert(state.model.Toggle(801));
        if(id==400 || id==401 || id>=460&&id<466)state.dialog=Dialog::Save;
        if(id==402)state.dialog=Dialog::Delete;
        for(int& page:state.gridPage)page=1;
        Add(id,20,40,49,19);MouseMove(nullptr,nullptr,21,41);
        assert(sounds.size()==1&&sounds.back()==L"BtMouseOver");
        MouseMove(nullptr,nullptr,22,42);assert(sounds.size()==1);
        Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,22,42);assert(sounds.size()==1);
        Mouse(nullptr,nullptr,WM_LBUTTONUP,0,22,42);
        assert(sounds.size()>=2&&sounds[1]==L"BtMouseClick"&&std::count(sounds.begin(),sounds.end(),L"BtMouseClick")==1);
    }
    // Disabled controls, editors, and a press released outside the button do not click.
    Reset();Add(24,20,40,49,19,false);MouseMove(nullptr,nullptr,21,41);
    Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,21,41);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,21,41);assert(sounds.empty());
    state.controls.clear();Add(8,20,40,49,19);MouseMove(nullptr,nullptr,21,41);
    Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,21,41);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,21,41);assert(sounds.empty());
    state.controls.clear();Add(7,20,40,49,19);MouseMove(nullptr,nullptr,21,41);
    Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,21,41);MouseMove(nullptr,nullptr,80,41);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,80,41);
    assert(sounds.size()==1&&sounds.back()==L"BtMouseOver");
    // A fast second button click is WM_LBUTTONDBLCLK + UP, and must also click once.
    Reset();Add(301,20,40,49,19);
    Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,21,41);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,21,41);
    Mouse(nullptr,nullptr,WM_LBUTTONDBLCLK,0,21,41);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,21,41);
    assert(sounds.size()==2&&std::count(sounds.begin(),sounds.end(),L"BtMouseClick")==2);
    // Close/cancel remain interactive while waiting for the server; other buttons stay silent.
    for(int id:{1,400}){
        Reset();state.pending=true;Add(id,20,40,49,19);MouseMove(nullptr,nullptr,21,41);
        Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,21,41);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,21,41);
        assert(sounds.size()>=2&&sounds[0]==L"BtMouseOver"&&sounds[1]==L"BtMouseClick");
    }
    Reset();state.pending=true;Add(24,20,40,49,19);MouseMove(nullptr,nullptr,21,41);
    Mouse(nullptr,nullptr,WM_LBUTTONDOWN,0,21,41);Mouse(nullptr,nullptr,WM_LBUTTONUP,0,21,41);assert(sounds.empty());
    // Actual wheel callback: sound only when the page changes, never at a boundary.
    Reset();for(int i=0;i<100;i++)state.model.current.items[0].push_back(Make(600+i,1000000,-101));
    Wheel(nullptr,nullptr,400,150,-120);assert(state.gridPage[3]==1&&sounds.size()==1&&sounds.back()==L"Tab");
    Wheel(nullptr,nullptr,400,150,-120);assert(state.gridPage[3]==1&&sounds.size()==1);
    // Detail and tooltip preserve excluded wear, while editable legacy KEEP still means empty.
    for(auto item:{Make(701,1050000,-105,-105,2),Make(702,1050000,-105,-105,7),Make(0,1050000,-105,-105,0)}){
        Reset();state.page=1;state.model.original=true;state.model.current.items[2]={item};
        Outfit retained;retained.defined=true;retained.entries=state.model.SaveScope();state.model.current.outfits[0]=retained;
        auto view=state.model.PreviewOutfit(retained);auto detail=OutfitSlotDetail(-105,view);
        assert(detail.item==state.model.Worn(-105)&&!detail.missing&&PantsBlocked());
        Hover(5004,300,180);assert(tooltipItem==detail.item);state.page=0;Hover(5104,155,285);assert(tooltipItem==detail.item);
    }
    state.page=1;state.model.current.items[2][0]=Make(703,1040000,-105,-105);
    auto legacyView=state.model.PreviewOutfit(state.model.current.outfits[0]);
    assert(!OutfitSlotDetail(-105,legacyView).item&&!HoveredItem(5004)&&!PantsBlocked());
    // Invalid name acknowledges input, then reports one notice before overwrite confirmation.
    Reset();state.model.current.outfits[0].defined=true;BeginSave(0);state.outfitName.clear();sounds.clear();
    Activate(401);assert(state.dialog==Dialog::Save&&!state.dialogMessage.empty()&&sends==0&&sounds.size()==2&&sounds[0]==L"BtMouseClick"&&sounds.back()==L"DlgNotice");
    std::cout<<"PASS actual Mouse/Hit/Activate/Save callbacks, try-on apply/undo, outfit select/double-click and wrapped dialog text\n";
    std::cout<<"PASS preview-driven pants availability and native sound routing for success, refusal and pending input\n";
    std::cout<<"PASS hover invalidation, wheel bounds, retained equipment detail/tooltip and name validation\n";
    assert(nativeSoundCalls==nativeSoundReleases && nativeSoundCalls>100);
    std::cout<<"PASS real EXE PlaySE/wcslen/WCHAR append: ANSI failure reproduced, UTF-16 paths and ABI verified\n";
    std::cout<<"PASS all button families: mouse hover, immediate click, repeat click, disabled/editor/cancel/pending and redraw gaps\n";
}
