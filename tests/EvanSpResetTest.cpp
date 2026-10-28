#include "../ezorsia/EvanSpReset.cpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <cstdarg>
namespace {
void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
template<typename Value> void Set(void* object, size_t offset, Value value) {
    *reinterpret_cast<Value*>(static_cast<unsigned char*>(object) + offset) = value;
}
void Jump(DWORD address, void* target) {
    auto* site = reinterpret_cast<unsigned char*>(address);
    site[0] = 0xE9;
    *reinterpret_cast<DWORD*>(site + 1) = reinterpret_cast<DWORD>(target) - address - 5;
}
void* Target(DWORD address) {
    const auto* site = reinterpret_cast<unsigned char*>(address);
    Require(site[0] == 0xE8, "CALL patch, not JMP");
    return const_cast<unsigned char*>(site + 5 + *reinterpret_cast<const int*>(site + 1));
}
void* LoadImage(const char* filename) {
    auto* mapped = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(Native(0x400000)),
        0xA94000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Require(mapped == reinterpret_cast<void*>(Native(0x400000)), "reserve native fixture before file allocations");
    FILE* file = nullptr;
    Require(fopen_s(&file, filename, "rb") == 0, "open disk EXE as data only");
    fseek(file, 0, SEEK_END);
    std::vector<unsigned char> bytes(ftell(file));
    rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read EXE");
    fclose(file);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    Require(image->Signature == IMAGE_NT_SIGNATURE && image->OptionalHeader.ImageBase == 0x400000, "083 PE");
    Require(image->OptionalHeader.SizeOfImage == 0xA94000, "fixture image size");
    std::memcpy(mapped, bytes.data(), image->OptionalHeader.SizeOfHeaders);
    const auto* sections = IMAGE_FIRST_SECTION(image);
    for (unsigned index = 0; index < image->FileHeader.NumberOfSections; ++index)
        std::memcpy(mapped + sections[index].VirtualAddress, bytes.data() + sections[index].PointerToRawData,
            sections[index].SizeOfRawData);
    return mapped;
}


int notices = 0, expectedJob = 0, expectedStage = 0, expectedGroup = 0;
std::string formatted;
void* __cdecl Format(char** out, const char* format, ...) {
    char buffer[512]; va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    formatted = buffer; *out = buffer; return out;
}
int __cdecl Show(char*, int a, int b, int c, int d) {
    Require(a == 0 && b == 0 && c == 0 && d == 0, "notice ABI"); ++notices; return 1;
}
int __fastcall Job(void* chr, void*) { Require(chr == &expectedJob, "job receiver"); return expectedJob; }
__declspec(naked) void Finish() { __asm {

pop edi
pop esi
pop ebx
pop ebp
ret
} }
__declspec(naked) void EvanEnd() { __asm {

pop eax
cmp eax, 0d4h
jne fail
mov eax, esi
add eax,100h
jmp Finish
fail:
int 3
} }
__declspec(naked) void OrdinaryEnd() { __asm {

pop eax
cmp eax, 0d4h
jne fail
mov eax, esi
jmp Finish
fail:
int 3
} }
__declspec(naked) void RejectEnd() { __asm {

mov eax,-1
jmp Finish
} }
__declspec(naked) int RunGate(int unused, void* chr, int group, int stage) {
    __asm {

push ebp
mov ebp,esp
push ebx
push esi
push edi
mov esi,[ebp+10h]
mov eax,[ebp+14h]
mov ebx,0
mov edi,1
jmp Gate
}
}
void __fastcall Draw(void* dialog, void*, int stage) {
    Require(stage == expectedStage, "heading stage untouched");
    char* text = nullptr;
    FormatHeading(&text, "UI/UIWindow.img/Reset/SP/Tab/disabled/%d", stage);
}
__declspec(naked) void TabEnd() {
    __asm {
        pop eax
        pop edx
        lea ecx, [esp+8]
        cmp edx, ecx
        jne broken
        cmp dword ptr [esp], 0
        jne broken
        cmp dword ptr [esp+4], 0
        jne broken
        add esp, 8
        pop edi
        pop esi
        ret
    broken:
        int 3
    }
}
__declspec(naked) int RunTab(int pair) {
    __asm {
        push esi
        push edi
        mov esi, [esp+0ch]
        shl esi, 1
        xor edi, edi
        jmp TabNumber
    }
}
}
void __cdecl ClientLog::Append(Component, const char*, ...) {}

int main(int argc, char** argv) {
    Require(argc == 2, "EXE fixture argument");
    void* image = LoadImage(argv[1]);
    std::vector<Signature> signatures(std::begin(dependencies), std::end(dependencies));
    for (const auto& p : patches) signatures.push_back(p.signature);
    for (const auto& sig : signatures) {
        Require(std::memcmp(reinterpret_cast<void*>(sig.address), sig.expected, sig.size)==0, "exact disk signature");
        for (size_t i=0;i<sig.size;i++) {
            auto* site=reinterpret_cast<unsigned char*>(sig.address);
            site[i]^=1;
            Require(!EvanSpReset::Install(static_cast<HMODULE>(image)), "mismatch rejected");
            site[i]^=1;
            for(const auto& p:patches) Require(std::memcmp(reinterpret_cast<void*>(p.signature.address),p.signature.expected,p.signature.size)==0,"no partial installation");
        }
    }
    Require(EvanSpReset::Install(static_cast<HMODULE>(image)), "install");
    Jump(Native(0x8D8298), Job); Jump(Native(0xA0C7EB),EvanEnd);
    Jump(Native(0xA0C892),OrdinaryEnd); Jump(Native(0xA0C73D),RejectEnd);
    Jump(Native(0x445B4B),Format); Jump(Native(0x9929DD),Show);
    Jump(Native(0x8B4E13),Draw);
    Jump(Native(0x8B3E1E),TabEnd);
    for (int pair=0;pair<5;pair++) Require(RunTab(pair)==pair*2+1,"source tab number and native stack");
    int cases=0;
    for(int stage=1;stage<=10;stage++) {
        expectedJob=stage==1?2200:2208+stage;
        for(int group=0;group<9;group++) {
            int want=group>=4 && (group-4)*2+1<=stage ? 0x100+group-4 : -1;
            int before=notices;
            Require(RunGate(0,&expectedJob,group,stage)==want,"native Evan gate group / stack");
            Require(notices-before==(want<0),"notice only for invalid use"); ++cases;
        }
    }
    for(int stage=1;stage<=4;stage++) {
        expectedJob=stage==1?100:108+stage;
        for(int group=0;group<9;group++) {
            int want=group<4 && group<stage?group:-1;
            Require(RunGate(0,&expectedJob,group,stage)==want,"native ordinary gate"); ++cases;
        }
    }
    unsigned char dialog[0xCC]{};
    for(int stage=0;stage<10;stage++) {
        expectedStage=stage;
        *reinterpret_cast<int*>(dialog+0xC4)=(stage==0?2200:2209+stage)*10000+1000;
        DrawHeading(dialog,nullptr,stage);
        Require(formatted=="UI/UIWindow.img/SkillEx/Tab/disabled/"+std::to_string(stage+1),"all ten original target tabs");
        Require(!evanHeading,"heading scope restored");
    }
    expectedStage=3; *reinterpret_cast<int*>(dialog+0xC4)=1121000;
    DrawHeading(dialog,nullptr,3);
    Require(formatted=="UI/UIWindow.img/Reset/SP/Tab/disabled/3","ordinary heading unchanged");
    std::printf("PASS: signatures, %d native gate cases, notices, target headings\n",cases);
}
