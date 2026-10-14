// Executes the original v83 BSTR converter and real WZ components offline.
// No game entry point, login, network connection or resource writes.
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "LegacyStringConversionFix.h"
#include "MixedDyeResources.h"
#include "detours.h"
#pragma comment(lib, "ole32.lib")
using namespace MixedDyeResources;
static void Need(bool ok, const char* message) {
    if (!ok) { printf("FAIL %s error=%lu\n", message, GetLastError()); ExitProcess(2); }
}
constexpr DWORD delta = 0x30000000;
static DWORD Native(DWORD address) { return address + delta; }
static BYTE* buffer;
static size_t allocated;
static void* __stdcall Allocate(size_t size) {
    Need(size < 4096, "native BSTR allocation stays small");
    allocated = size;
    memset(buffer, 0, 4096);
    return buffer;
}
static BYTE* MapImage(const wchar_t* path) {
    FILE* file = nullptr; Need(!_wfopen_s(&file, path, L"rb"), "open EXE");
    fseek(file, 0, SEEK_END); std::vector<BYTE> bytes(ftell(file)); rewind(file);
    Need(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read EXE"); fclose(file);
    const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    Need(nt->FileHeader.TimeDateStamp == 0x4B7C15C9 && nt->OptionalHeader.SizeOfImage == 0xA94000, "v83 image");
    auto* image = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(Native(0x400000)),
        nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Need(image == reinterpret_cast<void*>(Native(0x400000)), "map isolated EXE");
    memcpy(image, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(image + section[i].VirtualAddress, bytes.data() + section[i].PointerToRawData, section[i].SizeOfRawData);
    // Only 0040638C (char* -> BSTR) and 00402B4F (allocation/length prefix)
    // execute. Relocate their three absolute API-slot operands, not their code.
    for (DWORD operand : {0x4063A6, 0x4063C7, 0x402B5C})
        *reinterpret_cast<DWORD*>(Native(operand)) += delta;
    *reinterpret_cast<void**>(Native(0xBF03D0)) = reinterpret_cast<void*>(&MultiByteToWideChar);
    *reinterpret_cast<void**>(Native(0xBF0550)) = reinterpret_cast<void*>(&Allocate);
    FlushInstructionCache(GetCurrentProcess(), image, nt->OptionalHeader.SizeOfImage);
    return image;
}
static BSTR Convert(const char* text) {
    return reinterpret_cast<BSTR(__stdcall*)(const char*)>(Native(0x40638C))(text);
}
static void SelectAddress(DWORD address) {
    if (buffer) VirtualFree(buffer, 0, MEM_RELEASE);
    buffer = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(address), 65536,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Need(buffer == reinterpret_cast<void*>(address), "allocate controlled address");
}
static void CheckNative(bool fixed, bool high) {
    for (const char* text : {"Lbracket", "info", "islot", "Shape2D#Vector2D", "UI/StatusBar.img/base/quickSlot", ""}) {
        SetLastError(0);
        BSTR result = Convert(text); DWORD error = GetLastError();
        Need(result == reinterpret_cast<BSTR>(buffer + 4), "native allocator and ownership unchanged");
        Need(allocated == strlen(text) * 2 + 6 && SysStringLen(result) == strlen(text), "native BSTR size and prefix");
        if (!fixed && high) Need(error == ERROR_INSUFFICIENT_BUFFER && result[0] == 0, "original high-address conversion fails");
        else {
            std::wstring expected(text, text + strlen(text));
            Need(result == expected && error == 0, "native conversion contains complete name");
        }
    }
    Need(Convert(nullptr) == nullptr, "native null input unchanged");
    printf("PASS native converter fixed=%d destination=%p high=%d\n", fixed, buffer + 4, high);
}
static HRESULT Resource(void* rm, BSTR key, VARIANT* result) {
    VARIANT missing{}; missing.vt = VT_ERROR; missing.scode = DISP_E_PARAMNOTFOUND;
    return Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT,VARIANT,VARIANT*)>(rm,0x1c)(rm,key,missing,missing,result);
}
static void CheckResources(void* rm, void* numbers, Factory factory, bool fixed) {
    for (const char* text : {"UI/StatusBar.img/gauge/graduation", "UI/StatusBar.img/base/quickSlot",
        "UI/UIWindow.img/MobGage/backgrnd", "UI/UIWindow.img/MobGage/backgrnd2",
        "UI/UIWindow.img/MobGage/backgrnd3", "UI/UIWindow.img/MobGage/backgrnd4"}) {
        BSTR key = Convert(text); Value value; Check(Resource(rm, key, &value.v));
        auto canvas = Query(value.v, CanvasIID());
        Need((canvas.p != nullptr) == fixed, "real resource Canvas type");
        if (!fixed) {
            auto* object = MixedDyeResources::Unknown(value.v);
            auto* ns = reinterpret_cast<BYTE*>(GetModuleHandleW(L"NameSpace.dll"));
            Need(object && ns && *reinterpret_cast<BYTE**>(object) == ns + 0x172dc, "old failure returns same NameSpace vtable as dump");
        }
        printf("resource fixed=%d path=%s canvas=%d\n", fixed, text, canvas.p != nullptr);
    }
    Value bracket; BSTR key = Convert("Lbracket");
    HRESULT hr = Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT*)>(numbers,0x14)(numbers,key,&bracket.v);
    auto canvas = Query(bracket.v, CanvasIID());
    Need(fixed ? hr == S_OK && canvas.p : hr == S_FALSE && bracket.v.vt == VT_EMPTY, "exact Lbracket failure/recovery");
    void* vector = nullptr;
    hr = factory(Convert("Shape2D#Vector2D"), &IID_IUnknown, &vector, nullptr);
    Need(fixed ? SUCCEEDED(hr) && vector : hr == HRESULT(0x800401f8) && !vector, "exact PCOM empty-class failure/recovery");
    if (vector) static_cast<IUnknown*>(vector)->Release();
    printf("PASS property and Vector2D fixed=%d factoryHr=%08lX\n", fixed, hr);
}

// An earlier ACP detour models WineCompatibility's chain. Its mapping must
// survive installation; no change to explicit UTF-8 or GBK semantics.
static decltype(&MultiByteToWideChar) beforeToWide = MultiByteToWideChar;
static decltype(&WideCharToMultiByte) beforeFromWide = WideCharToMultiByte;
static int WINAPI ChineseWide(UINT cp,DWORD f,LPCCH s,int n,LPWSTR d,int cap) {
    return beforeToWide(cp == CP_ACP ? 936 : cp,f,s,n,d,cap);
}
static int WINAPI ChineseNarrow(UINT cp,DWORD f,LPCWCH s,int n,LPSTR d,int cap,LPCCH def,LPBOOL used) {
    return beforeFromWide(cp == CP_ACP ? 936 : cp,f,s,n,d,cap,def,used);
}
static void InstallEarlierHooks() {
    Need(DetourTransactionBegin() == NO_ERROR, "begin earlier hook");
    Need(DetourUpdateThread(GetCurrentThread()) == NO_ERROR, "earlier hook thread");
    Need(DetourAttach(reinterpret_cast<PVOID*>(&beforeToWide), ChineseWide) == NO_ERROR, "earlier ACP wide");
    Need(DetourAttach(reinterpret_cast<PVOID*>(&beforeFromWide), ChineseNarrow) == NO_ERROR, "earlier ACP narrow");
    Need(DetourTransactionCommit() == NO_ERROR, "commit earlier hook");
}
static void CheckApi(bool chinese) {
    auto* wide = reinterpret_cast<wchar_t*>(buffer + 128);
    auto* narrow = reinterpret_cast<char*>(buffer + 512);
    for (int i = 0; i < 2000; ++i) {
        Need(MultiByteToWideChar(936,0,"\xD6\xD0\xCE\xC4",-1,wide,0x3fffffff) == 3 && !wcscmp(wide,L"\x4e2d\x6587"), "GBK sentinel conversion");
        BOOL used = TRUE;
        Need(WideCharToMultiByte(936,0,wide,-1,narrow,0x3fffffff,nullptr,&used) == 5 && !used && !strcmp(narrow,"\xD6\xD0\xCE\xC4"), "GBK reverse and default output");
        Need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,"\xE4\xB8\xAD",3,wide,0x3fffffff) == 1 && wide[0] == 0x4e2d, "explicit UTF8 input length");
        Need(WideCharToMultiByte(CP_UTF8,0,wide,1,narrow,0x3fffffff,nullptr,nullptr) == 3 && !memcmp(narrow,"\xE4\xB8\xAD",3), "explicit UTF8 reverse length");
    }
    if (chinese) Need(MultiByteToWideChar(CP_ACP,0,"\xD6\xD0",-1,wide,0x3fffffff) == 2 && wide[0] == 0x4e2d, "earlier ACP detour remains effective");
    SetLastError(0);
    Need(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,"\xFF",1,wide,0x3fffffff) && GetLastError() == ERROR_NO_UNICODE_TRANSLATION, "invalid UTF8 error retained");
    SetLastError(0);
    Need(!MultiByteToWideChar(936,0,"Lbracket",-1,wide,2) && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "ordinary short buffer still fails");
    Need(MultiByteToWideChar(936,0,"Lbracket",-1,nullptr,0) == 9, "ordinary size query unchanged");
    BOOL used = FALSE;
    Need(WideCharToMultiByte(20127,0,L"\x4e2d",-1,narrow,0x3fffffff,"?",&used) == 2 && used && narrow[0] == '?', "fallback character output retained");
    printf("PASS API matrix address=%p iterations=2000 earlierACP=%d\n", buffer, chinese);
}
int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout,nullptr,_IONBF,0); SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    Need(argc == 2 || argc == 3, "client directory argument");
    bool chinese = argc == 3;
    std::wstring exe = std::wstring(argv[1]) + L"\\BeiDou.exe";
    auto* image = MapImage(exe.c_str());
    Need(SetCurrentDirectoryW(argv[1]), "client directory"); Check(CoInitialize(nullptr));
    auto pc = LoadLibraryW(L"PCOM.dll"); Need(pc != nullptr, "PCOM");
    Check(reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(pc,"PcInitModule"))());
    auto factory = reinterpret_cast<Factory>(GetProcAddress(pc,"PcCreateObject"));
    const GUID rmId={0x57dfe40b,0x3e20,0x4dbc,{0x97,0xe8,0x80,0x5a,0x50,0xf3,0x81,0xbf}};
    const GUID nsId={0x2aeeeb36,0xa4e1,0x4e2b,{0x8f,0x6f,0x2e,0x7b,0xde,0xc5,0xc5,0x3d}};
    const GUID fsId={0x352d8655,0x51e4,0x4668,{0x8c,0xe4,0x08,0x66,0xe2,0xb6,0xa5,0xb5}};
    auto rm=New(factory,L"ResMan",rmId),ns=New(factory,L"NameSpace",nsId),fs=New(factory,L"NameSpace#FileSystem",fsId);
    Check(Method<HRESULT(__stdcall*)(void*,int,int,int)>(rm.p,0x14)(rm.p,0x11,-1,-1));
    Check(reinterpret_cast<HRESULT(__cdecl*)(void**,int)>(GetProcAddress(pc,"PcRootNameSpace"))(&ns.p,1));
    BSTR key=SysAllocString(L"Data");Check(Method<HRESULT(__stdcall*)(void*,BSTR)>(fs.p,0x34)(fs.p,key));SysFreeString(key);
    key=SysAllocString(L"/");Check(Method<HRESULT(__stdcall*)(void*,BSTR,void*,int)>(ns.p,0x18)(ns.p,key,fs.p,0));SysFreeString(key);
    key=SysAllocString(L"UI/StatusBar.img/number");Value number;Check(Resource(rm.p,key,&number.v));SysFreeString(key);
    auto numbers=Query(number.v,PropertyIID());Need(numbers.p != nullptr,"status number property");
    SelectAddress(0x20000000);CheckNative(false,false);
    SelectAddress(0x96080000);CheckNative(false,true);CheckResources(rm.p,numbers.p,factory,false);
    if (chinese) InstallEarlierHooks();
    Need(!LegacyStringConversionFix::Install(nullptr), "reject invalid image");
    Need(LegacyStringConversionFix::Install(reinterpret_cast<HMODULE>(image)), "install production fix");
    Need(LegacyStringConversionFix::Install(reinterpret_cast<HMODULE>(image)), "idempotent install");
    CheckNative(true,true);CheckResources(rm.p,numbers.p,factory,true);
    // >C0000000 also exercises wrap of the one-byte reverse conversion.
    for (DWORD address : {0x20000000ul,0x80000000ul,0x96080000ul,0xC1000000ul}) {
        SelectAddress(address);CheckNative(true,address >= 0x80000000);CheckApi(chinese);
    }
    puts("PASS legacy conversion root-cause regression");
    return 0;
}
