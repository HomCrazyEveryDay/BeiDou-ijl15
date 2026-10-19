// Exercise the actual production scopes without loading or executing BeiDou.exe.
// Only the two singleton data slots are mapped in this test process. Native game
// function addresses are never called; unused hook/graphics/log APIs are stubs.
#include "WardrobePreviewUnderTest.h"
#include <cassert>
#include <cstdio>
bool Memory::SetHook(bool,void**,void*){return false;}
void CrashReporter::RecordEvent(const char*,const char*,...){}
bool WardrobeClip::Install(void*){return false;}
bool WardrobeClip::Failed(){return false;}
void WardrobeClip::SetRoot(void*,int,int,int,int){}
static bool previewVisible=true;
void WardrobeClip::SetVisible(bool value){previewVisible=value;}
static unsigned retireCalls=0;
unsigned WardrobeClip::RetireTree(){assert(!p.visible&&!previewVisible);++retireCalls;return 3;}

static int releases=0,lookUpdates=0;
static void __fastcall ReleasePreviewSpace(void* ref,void*,int value){
    assert(value==0&&ref==p.space&&releases++==0);
    assert(p.user[1] && UserGlobal()==User() && !previewVisible && !p.visible);
    assert(retireCalls==1); // Native layers retire before either game object is released.
}
static void __fastcall ReleasePreviewUser(void* ref,void*){
    assert(ref==p.user&&releases++==1&&!p.space[1]);
    UserGlobal()=nullptr; // The real CUserPreview destructor clears its singleton.
}
static void __fastcall FailSpaceRelease(void*,void*,int){RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);}
static void __fastcall SetLookTest(void* avatar,void*,const void* look){assert(avatar==User()+0x88&&look==p.look);++lookUpdates;}
static void __fastcall RefreshRingsTest(void* user,void*){assert(user==User()&&lookUpdates==1);++lookUpdates;}
static void PreviewLifecycle(){
    p=Preview{};p.space[1]=0x3000;p.user[1]=0x4000;p.visible=true;
    releaseSpace=reinterpret_cast<ReleaseSpace>(&ReleasePreviewSpace);releaseUser=reinterpret_cast<ReleaseUser>(&ReleasePreviewUser);
    setAvatarLook=reinterpret_cast<SetAvatarLook>(&SetLookTest);refreshRings=reinterpret_cast<ReleaseUser>(&RefreshRingsTest);
    NativeCall(&ApplyLook);assert(lookUpdates==2);
    void* field=SpaceGlobal();void* user=UserGlobal();
    WardrobePreview::Dispose();assert(releases==2&&!p.user[1]&&!p.space[1]);
    assert(SpaceGlobal()==field&&UserGlobal()==user);
    WardrobePreview::Dispose();assert(releases==2); // Repeated Esc/close is idempotent.
    p.space[1]=0x3000;p.user[1]=0x4000;p.visible=true;previewVisible=true;
    releaseSpace=reinterpret_cast<ReleaseSpace>(&FailSpaceRelease);
    WardrobePreview::Dispose();assert(p.poisoned&&!previewVisible&&p.user[1]&&p.space[1]&&!WardrobePreview::Ready());
    WardrobePreview::Dispose();assert(releases==2); // Failed teardown never retries or exposes orphan children.
    assert(SpaceGlobal()==field&&UserGlobal()==user);p=Preview{};
}

static unsigned forwardedAvatar=0,forwardedZ=0;
static void __fastcall NativeAvatar(void* avatar,void*,void* origin,int x,int y,void* overlay,int z){
    assert(avatar==reinterpret_cast<void*>(0x4088)&&origin==reinterpret_cast<void*>(0x5000));
    assert(x==12&&y==34&&overlay==reinterpret_cast<void*>(0x6000)&&z==56);++forwardedAvatar;
}
static void __fastcall NativeZ(void* user,void*){assert(user==reinterpret_cast<void*>(0x4000));++forwardedZ;}
static void QuarantinedAddressReuse(){
    auto previousAvatar=avatarInit;auto previousZ=userZ;
    p=Preview{};p.poisoned=true;p.user[1]=0x4000;avatarInit=&NativeAvatar;userZ=&NativeZ;
    assert(User()==reinterpret_cast<unsigned char*>(0x4000));
    // A field user reusing the failed preview's address keeps its original layer,
    // z value and native callback; the stale identity must not match either hook.
    InitAvatar(reinterpret_cast<void*>(0x4088),nullptr,reinterpret_cast<void*>(0x5000),12,34,reinterpret_cast<void*>(0x6000),56);
    SetUserZ(reinterpret_cast<void*>(0x4000),nullptr);
    assert(forwardedAvatar==1&&forwardedZ==1);p=Preview{};avatarInit=previousAvatar;userZ=previousZ;
}
static void NestedFieldCallback(){
    assert(WardrobePreview::Busy());
    assert(SpaceGlobal()==reinterpret_cast<void*>(0x3000));
    WardrobeInput::PreviewScope input;
    {
        WardrobePreview::ExternalScope outer;
        assert(SpaceGlobal()==reinterpret_cast<void*>(0x1000));
        {WardrobeInput::FieldScope probe;assert(probe.previous==0);}
        {
            WardrobePreview::ExternalScope inner;
            SpaceGlobal()=reinterpret_cast<void*>(0x5000);UserGlobal()=reinterpret_cast<void*>(0x6000);
        }
        assert(SpaceGlobal()==reinterpret_cast<void*>(0x5000));
        bool denied=false;try{NativeCall([](){});}catch(...){denied=true;}assert(denied);
    }
    assert(SpaceGlobal()==reinterpret_cast<void*>(0x3000));
    {WardrobeInput::FieldScope probe;assert(probe.previous==1);}
}
static void FaultInField(){
    WardrobeInput::PreviewScope input;
    WardrobePreview::ExternalScope outer;
    WardrobePreview::ExternalScope inner;
    SpaceGlobal()=reinterpret_cast<void*>(0x7000);UserGlobal()=reinterpret_cast<void*>(0x8000);
    RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
}
static LONG nativeX=-300,nativeY=-125,windowX=10,windowY=20;
static HRESULT positionResult=S_OK;
static unsigned positionCalls=0;
static HRESULT __stdcall NativePosition(void*,LONG* x,LONG* y,LONG* rx,LONG* ry,LONG*,LONG*,double*,double*,VARIANT time){
    assert(time.vt==VT_I4 && time.lVal==123);++positionCalls;
    if(x)*x=nativeX;if(y)*y=nativeY;if(rx)*rx=nativeX;if(ry)*ry=nativeY;return positionResult;
}
static HRESULT __stdcall WindowX(void*,LONG* x){*x=windowX;return S_OK;}
static HRESULT __stdcall WindowY(void*,LONG* y){*y=windowY;return S_OK;}
static void NativeInterpolation(){
    unsigned char user[0x11a8]{};void* controller=reinterpret_cast<void*>(0x1234000);
    *reinterpret_cast<void**>(user+0x11a4)=controller;p.user[1]=reinterpret_cast<DWORD>(user);
    void* methods[11]{};methods[0x20/4]=reinterpret_cast<void*>(&WindowX);methods[0x28/4]=reinterpret_cast<void*>(&WindowY);
    auto table=methods;p.overlay=&table;p.visible=true;snapshot=&NativePosition;
    LONG x=0,y=0,rx=0,ry=0;VARIANT time{};time.vt=VT_I4;time.lVal=123;
    assert(SUCCEEDED(DisplaySnapshot(controller,&x,&y,&rx,&ry,nullptr,nullptr,nullptr,nullptr,time)));
    assert(x==148 && y==351 && rx==x && ry==y);
    nativeX=-297;nativeY=-126; // A second native render sample, with no physics/UI update between.
    assert(SUCCEEDED(DisplaySnapshot(controller,&x,&y,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,time)));
    assert(x==151 && y==350 && positionCalls==2);
    windowX+=50;windowY+=30;
    assert(SUCCEEDED(DisplaySnapshot(controller,nullptr,nullptr,&rx,&ry,nullptr,nullptr,nullptr,nullptr,time)));
    assert(rx==201 && ry==380); // Name/effect origin follows a dragged window immediately.
    DisplaySnapshot(reinterpret_cast<void*>(0x9999),&x,&y,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,time);
    assert(x==nativeX && y==nativeY); // Every other player/controller uses the untouched original.
    p.visible=false;DisplaySnapshot(controller,&x,&y,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,time);
    assert(x<-30000 && y<-30000);
    p.visible=true;positionResult=E_FAIL;
    assert(FAILED(DisplaySnapshot(controller,&x,&y,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,time)));
    assert(x==nativeX && y==nativeY);
    // A native destructor may fail after freeing its user. Subsequent snapshots
    // of unrelated field controllers must not touch that quarantined pointer.
    auto inaccessible=VirtualAlloc(nullptr,0x2000,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);assert(inaccessible);
    p.poisoned=true;p.user[1]=reinterpret_cast<DWORD>(inaccessible);positionResult=S_OK;
    assert(SUCCEEDED(DisplaySnapshot(controller,&x,&y,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,time)));
    assert(x==nativeX && y==nativeY);VirtualFree(inaccessible,0,MEM_RELEASE);p.poisoned=false;
    p.user[1]=0;p.overlay=nullptr;
    puts("PASS: actual render snapshot forwarding, native sub-tick samples, translation, null outputs and foreign controller isolation");
}
int main(){
    auto region=VirtualAlloc(reinterpret_cast<void*>(0x30be0000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(region==reinterpret_cast<void*>(0x30be0000));
    p.thread=GetCurrentThreadId();p.space[1]=0x3000;p.user[1]=0x4000;
    SpaceGlobal()=reinterpret_cast<void*>(0x1000);UserGlobal()=reinterpret_cast<void*>(0x2000);
    NativeCall(&NestedFieldCallback);
    assert(SpaceGlobal()==reinterpret_cast<void*>(0x5000)&&UserGlobal()==reinterpret_cast<void*>(0x6000));
    assert(!WardrobePreview::Busy()&&externalDepth==0);
    bool caught=false;try{NativeCall(&FaultInField);}catch(...){caught=true;}assert(caught);
    assert(SpaceGlobal()==reinterpret_cast<void*>(0x7000)&&UserGlobal()==reinterpret_cast<void*>(0x8000));
    assert(!WardrobePreview::Busy()&&externalDepth==0);
    {WardrobeInput::FieldScope probe;assert(probe.previous==0);}
    PreviewLifecycle();
    p.space[1]=p.user[1]=0;VirtualFree(region,0,MEM_RELEASE);
    NativeInterpolation();
    QuarantinedAddressReuse();
    puts("PASS: nested field callbacks preserve replacement singletons, input isolation, reentry guard and SEH cleanup");
}
