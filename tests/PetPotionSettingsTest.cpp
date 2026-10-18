#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <vector>
#include <string>
#include "../ezorsia/ClientLog.h"

static std::wstring testRoot, privateKey;
static HKEY legacyKey = nullptr;
static bool denyRead = false, denyWrite = false;
static unsigned writes = 0, checks = 0, nativeLoads = 0;
static void Require(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::printf("FAIL %s error=%lu\n", message, GetLastError()); std::exit(1); }
}
namespace ClientLog {
void Append(Component, const char* format, ...) {
    va_list args;va_start(args,format);std::vprintf(format,args);va_end(args);std::puts("");
}
}
static LSTATUS WINAPI TestOpen(HKEY root, LPCWSTR path, DWORD options, REGSAM access, PHKEY key) {
    Require(root == HKEY_CURRENT_USER && !wcscmp(path,L"Software\\BeiDou\\MapleStory\\PetPotion"), "private reads use HKCU BeiDou only");
    if (denyRead) return ERROR_ACCESS_DENIED;
    return RegOpenKeyExW(root, privateKey.c_str(), options, access, key);
}
static LSTATUS WINAPI TestCreate(HKEY root,LPCWSTR path,DWORD reserved,LPWSTR cls,DWORD options,
    REGSAM access,const LPSECURITY_ATTRIBUTES security,PHKEY key,LPDWORD disposition) {
    Require(root == HKEY_CURRENT_USER && !wcscmp(path,L"Software\\BeiDou\\MapleStory\\PetPotion"), "private writes use HKCU BeiDou only");
    ++writes;
    if (denyWrite) return ERROR_ACCESS_DENIED;
    Require(options == REG_OPTION_NON_VOLATILE, "production settings survive Windows restart");
    // The isolated test subtree is volatile; never create persistent test data.
    return RegCreateKeyExW(root,privateKey.c_str(),reserved,cls,REG_OPTION_VOLATILE,access,security,key,disposition);
}
// Use real registry values under a disposable test-only key. Only the two
// namespace entry points are redirected; production handles are never opened.
#define RegOpenKeyExW TestOpen
#define RegCreateKeyExW TestCreate
#include "../ezorsia/PetPotionSettings.cpp"
#undef RegOpenKeyExW
#undef RegCreateKeyExW

static BYTE config[0x300]{};
static DWORD loadEntry = 0, writeEntry = 0;
static const char hpName[] = "soHPFlash";
static int& Hp() { return *reinterpret_cast<int*>(config+0x80); }
static int& Mp() { return *reinterpret_cast<int*>(config+0x84); }
static void __fastcall NativeLoad(void* self, void*) {
    Require(self == config, "native load keeps config instance");
    ++nativeLoads;
    Hp()=0;Mp()=0;
    *reinterpret_cast<int*>(config+0x68)=17; // unrelated native audio preference
}
__declspec(naked) static void InvokeLoad() {
    __asm {
        lea ecx, config
        call dword ptr [loadEntry]
        ret
    }
}
__declspec(naked) static void InvokeLegacyWrite() {
    __asm {
        push offset complete
        push 19
        push offset hpName
        push 0
        lea ecx, config
        jmp dword ptr [writeEntry]
    complete:
        ret
    }
}
static BYTE* Map(const char* path) {
    FILE* file = nullptr; Require(!fopen_s(&file,path,"rb"),"open EXE");
    fseek(file,0,SEEK_END);std::vector<BYTE> bytes(ftell(file));rewind(file);
    Require(fread(bytes.data(),1,bytes.size(),file)==bytes.size(),"read EXE");fclose(file);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    auto base=static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(0x30400000),
        nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Require(base==reinterpret_cast<void*>(0x30400000),"isolated EXE map");
    memcpy(base,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    auto section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(base+section[i].VirtualAddress,bytes.data()+section[i].PointerToRawData,section[i].SizeOfRawData);
    return base;
}
static void Legacy(int hp,int mp) {
    for (auto value : {std::make_pair("soHPFlash",hp),std::make_pair("soMPFlash",mp)}) {
        const DWORD number=static_cast<DWORD>(value.second);
        Require(RegSetValueExA(legacyKey,value.first,0,REG_DWORD,reinterpret_cast<const BYTE*>(&number),4)==ERROR_SUCCESS,"test legacy write");
    }
}
static void ClearPrivate() {
    const LSTATUS result=RegDeleteTreeW(HKEY_CURRENT_USER,privateKey.c_str());
    Require(result==ERROR_SUCCESS||result==ERROR_FILE_NOT_FOUND,"clear only private test key");
    haveSaved=false;persistenceAvailable=true;
}
static void WritePrivate(DWORD value,DWORD type=REG_DWORD) {
    HKEY key=nullptr;
    Require(RegCreateKeyExW(HKEY_CURRENT_USER,privateKey.c_str(),0,nullptr,REG_OPTION_VOLATILE,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS,"create test private key");
    Require(RegSetValueExW(key,L"AlertStepsV1",0,type,reinterpret_cast<const BYTE*>(&value),4)==ERROR_SUCCESS,"write test private value");
    RegCloseKey(key);
}
static DWORD Stored() {
    HKEY key=nullptr;DWORD type=0,size=4,value=0;
    Require(RegOpenKeyExW(HKEY_CURRENT_USER,privateKey.c_str(),0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS,"read test private key");
    Require(RegQueryValueExW(key,L"AlertStepsV1",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4,"read versioned pair");
    RegCloseKey(key);return value;
}
static void ExpectLoad(int hp,int mp,const char* name) {
    const unsigned before=nativeLoads; SetLastError(0x1234);
    InvokeLoad();
    Require(GetLastError()==0x1234,"extra registry work preserves last error");
    Require(nativeLoads==before+1&&Hp()==hp&&Mp()==mp,name);
    Require(*reinterpret_cast<int*>(config+0x68)==17,"unrelated native settings retained");
    std::printf("PASS %s hp=%d mp=%d\n",name,Hp(),Mp());
}
int main(int argc,char** argv) {
    Require(argc==2,"EXE path");
    testRoot=L"Software\\BeiDouTests\\PetPotionSettings-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    privateKey=testRoot+L"\\Private";
    std::atexit([] {
        if (legacyKey) { RegCloseKey(legacyKey);legacyKey=nullptr; }
        if (testRoot.rfind(L"Software\\BeiDouTests\\PetPotionSettings-",0)==0)
            RegDeleteTreeW(HKEY_CURRENT_USER,testRoot.c_str());
    });
    Require(RegCreateKeyExW(HKEY_CURRENT_USER,(testRoot+L"\\Legacy").c_str(),0,nullptr,REG_OPTION_VOLATILE,
        KEY_READ|KEY_WRITE,nullptr,&legacyKey,nullptr)==ERROR_SUCCESS,"create scoped legacy test key");
    *reinterpret_cast<HKEY*>(config+0xc0)=legacyKey;
    auto base=Map(argv[1]);const std::vector<BYTE> before(base+0x9c000,base+0x9d000);
    Require(!PetPotionSettings::Install(nullptr),"null module rejected");
    base[0x9cc24]^=1;
    Require(!PetPotionSettings::Install(reinterpret_cast<HMODULE>(base)),"mismatched final site rejected");
    base[0x9cc24]^=1;
    Require(!memcmp(before.data(),base+0x9c000,before.size()),"failed install makes no partial patch");
    Require(PetPotionSettings::Install(reinterpret_cast<HMODULE>(base)),"install all three call sites");
    Require(PetPotionSettings::Install(reinterpret_cast<HMODULE>(base)),"idempotent install");
    for(size_t i=0;i<before.size();++i) {
        const size_t rva=0x9c000+i;
        const bool patched=(rva>=0x9c2ff&&rva<0x9c304)||(rva>=0x9cbe2&&rva<0x9cbe7)||(rva>=0x9cc23&&rva<0x9cc28);
        if(!patched)Require(base[rva]==before[i],"only HP/MP-specific calls changed");
    }
    originalLoad=reinterpret_cast<LoadGlobal>(NativeLoad);
    loadEntry=reinterpret_cast<DWORD>(base+0x9c2ff);
    base[0x9c304]=0xc3;base[0x9cbe7]=0xc3;base[0x9cc28]=0xc3;
    FlushInstructionCache(GetCurrentProcess(),base+0x9c000,0x1000);
    Legacy(19,7);ExpectLoad(19,7,"existing nonzero preferences migrate");
    Require(Stored()==Encode({19,7}),"migration stored as one versioned value");
    Legacy(0,0);ExpectLoad(19,7,"other server changes no longer affect BeiDou");
    const unsigned previousWrites=writes;PetPotionSettings::Save(config);
    Require(writes==previousWrites,"unchanged settings avoid repeated writes");
    Hp()=0;Mp()=16;SetLastError(0x9876);PetPotionSettings::Save(config);
    Require(GetLastError()==0x9876,"save preserves last error");
    ExpectLoad(0,16,"explicit HP disable survives restart");
    Hp()=0;Mp()=0;PetPotionSettings::Save(config);ExpectLoad(0,0,"explicit double disable survives restart");
    Hp()=20;PetPotionSettings::Save(config);Require(Stored()==Encode({0,0}),"invalid runtime value not persisted");
    ClearPrivate();Legacy(0,19);ExpectLoad(10,19,"first migration repairs only zero HP");
    ClearPrivate();Legacy(19,0);ExpectLoad(19,10,"first migration repairs only zero MP");
    ClearPrivate();Legacy(100,-1);ExpectLoad(10,10,"foreign out-of-range values use defaults");
    ClearPrivate();RegDeleteValueA(legacyKey,"soHPFlash");RegDeleteValueA(legacyKey,"soMPFlash");
    ExpectLoad(10,10,"new player without legacy settings gets 50 percent");
    ClearPrivate();Legacy(19,19);*reinterpret_cast<HKEY*>(config+0xc0)=nullptr;
    ExpectLoad(10,10,"unavailable native HKLM handle still initializes alerts");
    *reinterpret_cast<HKEY*>(config+0xc0)=legacyKey;
    for (DWORD invalid : {0x02001313u,0x0100ffffu,0u}) {
        WritePrivate(invalid);ExpectLoad(10,10,"invalid private setting does not re-import foreign values");
    }
    WritePrivate(0x01001313,REG_SZ);ExpectLoad(10,10,"wrong private type recovers");
    WritePrivate(Encode({19,15}));denyRead=true;ExpectLoad(10,10,"private read failure keeps usable session defaults");
    Hp()=12;Mp()=13;PetPotionSettings::Save(config);denyRead=false;
    Require(Stored()==Encode({19,15}),"unreadable saved preference is not overwritten");
    ExpectLoad(19,15,"saved preference restored after access recovers");
    ClearPrivate();Legacy(0,0);denyWrite=true;ExpectLoad(10,10,"write denied does not disable current session");
    denyWrite=false;Hp()=14;Mp()=8;PetPotionSettings::Save(config);
    ExpectLoad(14,8,"save retries successfully after write access recovers");
    for(DWORD site : {0x9cbe2u,0x9cc23u}) {
        writeEntry=reinterpret_cast<DWORD>(base+site);InvokeLegacyWrite();
    }
    DWORD type=0,size=4,legacyHp=99;
    Require(RegQueryValueExA(legacyKey,"soHPFlash",nullptr,&type,reinterpret_cast<BYTE*>(&legacyHp),&size)==ERROR_SUCCESS&&legacyHp==0,
        "both native HP/MP write call sites leave shared legacy setting untouched");
    RegCloseKey(legacyKey);legacyKey=nullptr;
    Require(testRoot.rfind(L"Software\\BeiDouTests\\PetPotionSettings-",0)==0,"cleanup path constrained to this test");
    Require(RegDeleteTreeW(HKEY_CURRENT_USER,testRoot.c_str())==ERROR_SUCCESS,"remove only this test's registry subtree");
    std::printf("PASS %u assertions; native call sites and real scoped registry persistence; no game entry or network\n",checks);
}
