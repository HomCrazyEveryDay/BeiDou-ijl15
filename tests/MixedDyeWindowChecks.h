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
    std::puts("PASS dedicated window assets, Unicode text, avatar ABI/refcounts, two-step confirm, same-color rejection, malformed packets");
}
