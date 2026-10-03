// Run the production UI code with real PCOM art/text and isolated native avatar
// ABI stubs. The game executable is mapped as data; its entry point is never run.
static int avatarCreates=0,avatarDeletes=0,avatarInits=0,windowPackets=0;
static unsigned char avatarLooks[2][0x1c5]{};
static void __fastcall MakeAvatar(DWORD* ref,void*){Require(!ref[1],"release old preview before recreate");ref[1]=(DWORD)new BYTE[0x1140]{};++avatarCreates;}
static void __fastcall DropAvatar(DWORD* ref,void*,int flag){Require(flag==0,"ZRef release ABI");delete[] reinterpret_cast<BYTE*>(ref[1]);++avatarDeletes;}
static void __fastcall InitAvatar(void* avatar,void*,const void* look,int action,void* vector,void* layer,int enabled,int x,int y,int scale,int option) {
    Require(avatar && action==5 && vector && layer && enabled==1 && (x==121 || x==239) && y==198 && scale==100 && option==0,"native CAvatar Init 9-argument ABI");
    std::memcpy(avatarLooks[x==239],look,0x1c5);++avatarInits;
    DreamCanvas::Release(vector);DreamCanvas::Release(layer);
}
static void __fastcall WindowPacket(void*,void*,Packet* packet){
    Require(packet->size==12 && packet->offset==0 && *reinterpret_cast<WORD*>(packet->data)==0x1004,"dedicated action packet size and opcode");
    Require(packet->data[4]==1 && *reinterpret_cast<int*>(packet->data+6)==state.token,"dedicated action version and session");++windowPackets;
}
static void __fastcall DummyInvalidate(void*,void*,const RECT*){}
static void* windowCanvas=nullptr;
static DWORD* __fastcall OutputCanvas(void*,void*,DWORD* out){*out=(DWORD)windowCanvas;static_cast<IUnknown*>(windowCanvas)->AddRef();return out;}
static int __fastcall WindowCoordinate(void*,void*){return 0;}
static int closedWindows=0;
static DWORD inventoryListNode=0;
static void __fastcall RestoreFocus(void* manager,void*,void* target){*reinterpret_cast<void**>(static_cast<BYTE*>(manager)+0x88)=target;}
static void __fastcall UnregisterWindow(void* window,void*) {
    Require(window==state.window && !state.shown && !state.ready,"close unregisters the hidden window once");
    for(auto& avatar:state.avatars)Require(!avatar[1],"close releases previews before destroying layers");
    auto manager=*reinterpret_cast<BYTE**>(0x30BEC20C);
    Require(*reinterpret_cast<void**>(manager+0x88)!=state.window+4,"close releases owned keyboard focus");
    Require(*reinterpret_cast<void**>(manager+0x90)!=state.window+4,"close releases owned mouse capture");
    *reinterpret_cast<void**>(manager+0x8c)=nullptr;
    *reinterpret_cast<DWORD*>(0x30BF1658)=inventoryListNode;
    ++closedWindows;
}
static void CheckWindowInputLifetime() {
    // Execute the original CWndMan point lookup, with a mixed window overlapping
    // an inventory. Native hit testing ignores layer visibility for bare CWnds.
    Require(*reinterpret_cast<DWORD*>(0x309E435C)==0x00BF1658,"native window-list operand");
    *reinterpret_cast<DWORD*>(0x309E435C)=0x30BF1658;
    Jump(reinterpret_cast<void*>(0x309E3264),RestoreFocus);
    Jump(reinterpret_cast<void*>(0x309E00AF),UnregisterWindow);
    BYTE manager[0x120]{},window[0x100]{},inventory[0x100]{};
    DWORD mixedNode[5]{},inventoryNode[5]{},inventoryMain[14]{},inventoryUi[19]{};
    std::memcpy(inventoryMain,state.mainTable,sizeof(inventoryMain));
    std::memcpy(inventoryUi,state.uiTable,sizeof(inventoryUi));
    inventoryMain[9]=0x30424461;inventoryUi[11]=inventoryUi[12]=(DWORD)&WindowCoordinate;
    state.uiTable[11]=state.uiTable[12]=(DWORD)&WindowCoordinate;
    *reinterpret_cast<DWORD*>(window)=(DWORD)state.mainTable;*reinterpret_cast<DWORD*>(window+4)=(DWORD)state.uiTable;
    *reinterpret_cast<DWORD*>(inventory)=(DWORD)inventoryMain;*reinterpret_cast<DWORD*>(inventory+4)=(DWORD)inventoryUi;
    for(auto target:{window,inventory}){*reinterpret_cast<int*>(target+0x24)=368;*reinterpret_cast<int*>(target+0x28)=259;}
    inventoryNode[4]=(DWORD)inventory;inventoryListNode=(DWORD)&inventoryNode[4];
    mixedNode[2]=(DWORD)inventoryNode;mixedNode[4]=(DWORD)window;
    *reinterpret_cast<void**>(0x30BEC20C)=manager;state.window=window;
    auto underPoint=reinterpret_cast<void*(__thiscall*)(void*,int,int)>(0x309E42B2);
    auto nativeHit=reinterpret_cast<int(__thiscall*)(void*,int,int,void**)>(0x30424461);
    state.ready=true;state.shown=false;state.token=17;
    *reinterpret_cast<DWORD*>(0x30BF1658)=(DWORD)&mixedNode[4];
    Require(nativeHit(window,100,100,nullptr)!=0,"reproduce original hidden rectangle intercepting inventory");
    Require(underPoint(manager,100,100)==inventory+4,"hidden mixed window passes native mouse lookup to inventory");
    Require(!OnFocus(window+4,nullptr,1),"hidden window cannot regain keyboard focus");
    state.shown=true;
    Require(underPoint(manager,100,100)==window+4,"visible mixed window still receives input");
    Require(underPoint(manager,368,100)==nullptr && underPoint(manager,-1,100)==nullptr,"native window bounds preserved");
    for(int reason=0;reason<6;++reason) {
        state.ready=state.shown=true;state.token=17;state.pending=state.confirm=false;state.avatarDirty=false;
        state.openedAt=GetTickCount();state.pressed=state.hover=-1;
        *reinterpret_cast<DWORD*>(0x30BF1658)=(DWORD)&mixedNode[4];
        *reinterpret_cast<void**>(manager+0x88)=window+4;
        *reinterpret_cast<void**>(manager+0x8c)=window+4;
        // In the final case another UI owns focus/capture; never clear its input.
        *reinterpret_cast<void**>(manager+0x90)=reason==5?inventory+4:window+4;
        if(reason==5)*reinterpret_cast<void**>(manager+0x88)=inventory+4;
        if(reason<2) {int x=reason==0?320:350,y=reason==0?236:8;Mouse(window+4,nullptr,WM_LBUTTONDOWN,0,x,y);Mouse(window+4,nullptr,WM_LBUTTONUP,0,x,y);}
        else if(reason==2)Key(window+4,nullptr,VK_ESCAPE,0);
        else if(reason==3) {snapshots.push_back(Snapshot{1,17});Tick(window);}
        else if(reason==4) {state.pending=true;state.sentAt=GetTickCount()-10001;Tick(window);}
        else MixedDyeWnd::OnFieldDispose();
        Require(closedWindows==reason+1 && !state.ready && !state.shown && !state.token,"every close path destroys window registration");
        Require(*reinterpret_cast<void**>(manager+0x88)==(reason==5?inventory+4:manager+4),"focus returns to default or preserves another owner");
        Require(*reinterpret_cast<void**>(manager+0x90)==(reason==5?inventory+4:nullptr),"mouse capture ownership respected");
        Require(underPoint(manager,100,100)==inventory+4,"inventory receives mouse after close");
        int packetsBefore=windowPackets;Mouse(window+4,nullptr,WM_LBUTTONDOWN,0,320,236);MouseMove(window+4,nullptr,320,236);
        Require(state.pressed==-1 && state.hover==-1 && windowPackets==packetsBefore,"stale hidden input cannot mutate state");
        Close();Require(closedWindows==reason+1,"repeated close is harmless");
    }
    state.window=nullptr;*reinterpret_cast<void**>(0x30BEC20C)=nullptr;*reinterpret_cast<DWORD*>(0x30BF1658)=0;
    std::puts("PASS real native mouse lookup: hidden window pass-through, cancel/close/Esc/result/timeout/map cleanup and input ownership");
}
static void CheckDedicatedWindow(void* rm,Factory factory) {
    *reinterpret_cast<DWORD*>(0x30B404E4)=0x309E067E;
    Require(MixedDyeWnd::Install(),"dedicated window native signature gate");
    *reinterpret_cast<void**>(0x30BF14E8)=rm;
    Jump(reinterpret_cast<void*>(0x30428967),MakeAvatar);Jump(reinterpret_cast<void*>(0x30428C15),DropAvatar);
    Jump(reinterpret_cast<void*>(0x3045149F),InitAvatar);Jump(reinterpret_cast<void*>(0x309E04C9),DummyInvalidate);
    Jump(reinterpret_cast<void*>(0x309E0502),DummyInvalidate);Jump(reinterpret_cast<void*>(0x30425C4C),OutputCanvas);
    Jump(reinterpret_cast<void*>(0x3049637B),WindowPacket);
    auto layer=New(factory,L"Shape2D#Vector2D",VectorIID());BYTE window[0x100]{};
    *reinterpret_cast<void**>(window+0x18)=layer.p;state.window=window;state.ready=true;state.token=17;
    auto draw=New(factory,L"Canvas",CanvasIID());
    Check(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(draw.p,0x2c)(draw.p,368,259,Integer(0),Integer(2)));windowCanvas=draw.p;
    for(int coupon:{5151040,5152302}) {
        const bool face=coupon==5152302;state.coupon=coupon;state.original=face?21303:63800;state.originalSecondary=-1;
        state.mask=255;state.primary=state.secondary=-1;state.editing=0;state.confirm=state.pending=state.hideHat=false;
        std::memset(state.look,0,sizeof(state.look));state.look[0xc]=1;
        *reinterpret_cast<int*>(state.look+0xd)=2;*reinterpret_cast<int*>(state.look+0x11)=21303;
        *reinterpret_cast<int*>(state.look+0x19)=63800;*reinterpret_cast<int*>(state.look+0x1d)=1002200;
        *reinterpret_cast<int*>(state.look+0x21)=1012764;
        Select(0);Require(state.primary==0 && state.editing==1 && !state.confirm && !windowPackets,"primary selection never sends or consumes");
        Select(0);Require(state.secondary==-1,"same second color rejected");
        Select(2);Require(state.secondary==2 && state.editing==-1,"different secondary accepted");
        Avatars();Require(avatarLooks[0][0xc]==1 && *reinterpret_cast<int*>(avatarLooks[0]+0xd)==2,"preview preserves gender and skin");
        Require(*reinterpret_cast<int*>(avatarLooks[0]+(face?0x11:0x19))==state.original,"before preview preserves original");
        Require(*reinterpret_cast<unsigned*>(avatarLooks[1]+(face?0x11:0x19))==MixedDye::Encode(MixedDye::WithColor(state.original,0,face),2,face),"after preview uses selected mixed ID");
        Require(*reinterpret_cast<int*>(avatarLooks[1]+0x21)==1012764,"preview preserves face accessory");
        Select(23);Avatars();Require(*reinterpret_cast<int*>(avatarLooks[1]+0x1d)==0,"hat toggle affects preview only");
        Require(*reinterpret_cast<int*>(state.look+0x1d)==1002200,"original look stays untouched");
        for(int editing:{0,1,-1}){state.editing=editing;DrawBody(window,nullptr);}
        Select(20);Require(state.confirm && !state.pending && !windowPackets,"first confirmation only arms confirmation");
        DrawBody(window,nullptr);Select(20);Require(state.pending && windowPackets==1,"second confirmation sends once");
        Select(20);Require(windowPackets==1,"pending request disables repeat confirm");windowPackets=0;
        ReleaseAvatars();state.pending=state.confirm=false;
    }
    Require(avatarCreates==8 && avatarInits==8 && avatarDeletes==8,"all before/after preview references released");
    unsigned char snapshot[24]{0,0,0,0,0x0e,0x10,0x4d,0x44,1,0};
    int token=17,coupon=5152302,original=21303;std::memcpy(snapshot+10,&token,4);std::memcpy(snapshot+14,&coupon,4);std::memcpy(snapshot+18,&original,4);snapshot[22]=255;snapshot[23]=255;
    Snapshot decoded;Require(MixedDyeWindowModel::Decode(snapshot,24,decoded),"server snapshot parses");
    for(unsigned size=0;size<24;++size)Require(!MixedDyeWindowModel::Decode(snapshot,size,decoded),"truncated snapshot rejected");
    snapshot[23]=1;Require(!MixedDyeWindowModel::Decode(snapshot,24,decoded),"single-color snapshot rejected");
    auto text=TextCanvas(L"\u6df7\u5408\u8272\u77b3\u5b54",0xffffff);unsigned ink=0;
    for(int y=0;y<17;++y)for(int x=0;x<100;++x)ink+=Pixel(text.p,x,y,250,17)>>24?1:0;
    Require(ink>40,"Unicode Chinese text has visible glyphs");
    state.window=nullptr;state.ready=false;state.token=0;state.canvases.clear();state.texts.clear();
    CheckWindowInputLifetime();
    std::puts("PASS dedicated window assets, Unicode text, avatar ABI/refcounts, two-step confirm, same-color rejection, malformed packets");
}
