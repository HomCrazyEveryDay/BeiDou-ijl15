#include "../ezorsia/stdafx.h"
#include "../ezorsia/EvanRuntime.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>

// Execute the actual patched comparison gates in an isolated executable.
// Map original image bytes for signature validation; never run game startup.
__declspec(naked) DWORD CompareAt(DWORD site, DWORD skill) {
    __asm {
        push ebp
        sub esp, 24h
        lea ebp, [esp+14h]
        mov eax, [esp+30h]
        mov [ebp-14h], eax
        mov eax, [esp+2ch]
        call eax
        pushfd
        pop eax
        add esp, 24h
        pop ebp
        ret
    }
}
__declspec(naked) DWORD CompareRegister(DWORD site, DWORD skill) {
    __asm {
        mov eax, [esp+8]
        mov ecx, [esp+4]
        call ecx
        pushfd
        pop eax
        ret
    }
}
__declspec(naked) DWORD CompareBook(DWORD site, DWORD item) {
    __asm {
        mov eax, [esp+8]
        xor edx, edx
        mov ecx, 10000
        div ecx
        mov ecx, [esp+4]
        call ecx
        pushfd
        pop eax
        ret
    }
}
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    // Reserve native addresses before loading the large PE file into a heap buffer.
    for(DWORD page:{0x104e0000,0x104f0000,0x10750000,0x10760000,0x10950000,0x10960000,0x10a00000})
        if(VirtualAlloc(reinterpret_cast<void*>(page),0x10000,MEM_RESERVE|MEM_COMMIT,
                PAGE_EXECUTE_READWRITE)!=reinterpret_cast<void*>(page)) { printf("Mapping failed at %08lx error %lu\n",page,GetLastError()); return 3; }

    std::ifstream file(argv[1],std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
    if(bytes.size()<4096)return 2;
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(bytes.data()+dos->e_lfanew);
    auto section=IMAGE_FIRST_SECTION(nt);
    for(DWORD page:{0x104e0000,0x104f0000,0x10750000,0x10760000,0x10950000,0x10960000,0x10a00000}) {
        BYTE* image=reinterpret_cast<BYTE*>(page);
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
            const DWORD rva=page-0x10400000;
            if(rva>=section[i].VirtualAddress && rva+0x10000<=section[i].VirtualAddress+section[i].SizeOfRawData)
                memcpy(image,bytes.data()+section[i].PointerToRawData+rva-section[i].VirtualAddress,0x10000);
        }
    }
    BYTE original=*reinterpret_cast<BYTE*>(0x10955edd);
    *reinterpret_cast<BYTE*>(0x10955edd)=0;
    if(EvanRuntime::Install() || *reinterpret_cast<BYTE*>(0x10955e26)!=0x0f) return 4;
    *reinterpret_cast<BYTE*>(0x10955edd)=original;
    if(!EvanRuntime::Install())return 5;
    auto mastery = reinterpret_cast<int(__cdecl*)(int)>(0x104e8f04);
    for(int skill:{22111001,22141002,22140000,22171002,22181003,1121000})
        if(mastery(skill)!=1)return 8;
    for(int skill:{22111000,22141001,22160000,20011000,22001001,1000,1001000,1101000})
        if(mastery(skill)!=0)return 9;
    for(DWORD site:{0x104f06c6,0x10a0a1cd}) {
        *reinterpret_cast<BYTE*>(site+5)=0xc3;
        for(DWORD item:{5620006,5620007,5620008,5620005,5620009,2280026,2290140,2000000}) {
            bool expected=item/10000==228 || (item>=5620006 && item<=5620008);
            if(((CompareBook(site,item)&0x40)!=0)!=expected)return 10;
        }
    }
    for(DWORD site:{0x10955edd,0x10955f1e,0x10955fb1}) {
        *reinterpret_cast<BYTE*>(site+7)=0xc3; // Return without changing flags.
        for(DWORD skill:{22181001,22171003,22171002,2121006,22001001}) {
            const bool equal=(CompareAt(site,skill)&0x40)!=0;
            if(equal!=(skill==22181001))return 6;
        }
    }
    // The early rectangular branch must select Flame Wheel, not Blaze.
    if(*reinterpret_cast<DWORD*>(0x10955e22)!=22171003 || *reinterpret_cast<WORD*>(0x10955e26)!=0x840f)return 7;
    *reinterpret_cast<BYTE*>(0x10955e26)=0xc3;
    for(DWORD skill:{22171003,22181001,22171002,2121006})
        if(((CompareRegister(0x10955e21,skill)&0x40)!=0)!=(skill==22171003))return 11;
    puts("PASS native image signatures, atomic rejection, Blaze-only chain and Flame Wheel rectangle gates, mastery predicate, cash-book dispatch, unrelated skills unchanged");
    for(DWORD page:{0x104e0000,0x104f0000,0x10750000,0x10760000,0x10950000,0x10960000,0x10a00000}) VirtualFree(reinterpret_cast<void*>(page),0,MEM_RELEASE);
    return 0;
}
