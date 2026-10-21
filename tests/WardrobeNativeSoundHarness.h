#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

// Run the production PlayUiSound against the supported EXE's actual PlaySE,
// wcslen and ZXString<WCHAR>::_Cat code without starting the game or audio.
// Substitute StringPool ownership, memcpy and the downstream CSoundMan service.
static std::vector<std::wstring> sounds;
static unsigned nativeSoundCalls=0,nativeSoundReleases=0;
namespace CrashReporter {
static void RecordEvent(const char*,const char*,...){std::abort();}
}
struct NativeSoundString {
    LONG refs;
    int capacity,byteLength;
    wchar_t text[256];
} nativeSoundString{};
static void SoundRequireAt(bool ok,int line){if(!ok){std::fprintf(stderr,"FAIL native sound ABI at %d\n",line);std::exit(1);}}
#define SoundRequire(ok) SoundRequireAt((ok),__LINE__)
static void SoundJump(DWORD from,void* to){
    auto code=reinterpret_cast<BYTE*>(from);code[0]=0xe9;
    *reinterpret_cast<DWORD*>(code+1)=reinterpret_cast<DWORD>(to)-from-5;
    FlushInstructionCache(GetCurrentProcess(),code,5);
}
static void* __cdecl SoundPool(){return &nativeSoundString;}
static DWORD* __fastcall SoundPrefix(void* pool,void*,DWORD* out,int id){
    SoundRequire(pool==&nativeSoundString && id==0x891);
    nativeSoundString.refs=1;nativeSoundString.capacity=255;
    wcscpy_s(nativeSoundString.text,L"Sound/UI.img/");
    nativeSoundString.byteLength=int(wcslen(nativeSoundString.text)*sizeof(wchar_t));
    *out=reinterpret_cast<DWORD>(nativeSoundString.text);return out;
}
static void __fastcall CaptureNativeSound(void* manager,void*,const wchar_t* path,int volume,int loop){
    SoundRequire(manager==&nativeSoundString && volume==100 && loop==0);
    SoundRequire(path==nativeSoundString.text && nativeSoundString.refs==1);
    const std::wstring full(path),prefix=L"Sound/UI.img/";
    SoundRequire(full.compare(0,prefix.size(),prefix)==0);
    SoundRequire(nativeSoundString.byteLength==int(full.size()*sizeof(wchar_t)));
    sounds.push_back(full.substr(prefix.size()));++nativeSoundCalls;
}
static void __fastcall ReleaseSound(DWORD* name,void*){
    SoundRequire(*name==reinterpret_cast<DWORD>(nativeSoundString.text));
    *name=0;++nativeSoundReleases;
}
static void InstallNativeSoundHarness(const wchar_t* path){
    static_assert(sizeof(void*)==4 && sizeof(wchar_t)==2,"native 083 UTF-16 ABI");
    static_assert(offsetof(NativeSoundString,text)==12,"ZXString header");
    FILE* file=nullptr;SoundRequire(_wfopen_s(&file,path,L"rb")==0);
    BYTE header[4096];SoundRequire(fread(header,1,sizeof(header),file)==sizeof(header));
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(header+reinterpret_cast<IMAGE_DOS_HEADER*>(header)->e_lfanew);
    SoundRequire(nt->FileHeader.TimeDateStamp==0x4B7C15C9 && nt->OptionalHeader.SizeOfImage==0xA94000
        && nt->OptionalHeader.ImageBase==0x400000);
    // Map away from the host and low process heaps; relative calls remain intact.
    auto image=static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(0x30400000),nt->OptionalHeader.SizeOfImage,
        MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    SoundRequire(image==reinterpret_cast<void*>(0x30400000));memcpy(image,header,sizeof(header));
    auto section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){
        SoundRequire(fseek(file,section[i].PointerToRawData,SEEK_SET)==0);
        SoundRequire(fread(image+section[i].VirtualAddress,1,section[i].SizeOfRawData,file)==section[i].SizeOfRawData);
    }
    fclose(file);
    // The EXE has no relocation table. Relocate the two absolute operands in
    // PlaySE; its relative calls to wcslen, append and buffer routines stay real.
    *reinterpret_cast<DWORD*>(0x30989589)+=0x30000000; // SEH handler
    *reinterpret_cast<DWORD*>(0x309895C3)+=0x30000000; // CSoundMan singleton
    SoundJump(0x3079E805,SoundPool);SoundJump(0x30406276,SoundPrefix);
    SoundJump(0x3043FB25,CaptureNativeSound);SoundJump(0x3040265E,ReleaseSound);
    // Native memcpy uses absolute jump tables; use the host CRT for byte copying.
    SoundJump(0x30A60C00,static_cast<void*(__cdecl*)(void*,const void*,size_t)>(memcpy));
    *reinterpret_cast<void**>(0x30BEBF94)=&nativeSoundString;
}
static void CheckNativeSoundEncoding(){
    // Reproduce the old const char* bug with padded storage (no out-of-bounds read).
    alignas(wchar_t) char ansi[32]="BtMouseOver";
    reinterpret_cast<void(__cdecl*)(const char*)>(0x30989588)(ansi);
    SoundRequire(sounds.size()==1 && sounds.back()!=L"BtMouseOver");
    reinterpret_cast<void(__cdecl*)(const wchar_t*)>(0x30989588)(L"BtMouseOver");
    SoundRequire(sounds.size()==2 && sounds.back()==L"BtMouseOver");
    sounds.clear();
}
