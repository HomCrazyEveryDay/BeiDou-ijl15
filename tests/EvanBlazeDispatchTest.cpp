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
// Execute the installed trampoline with an actual v83-shaped stack frame,
// avatar duration slot, and action frame array. No game process is involved.
__declspec(naked) int DurationAt(int skill, int speed, void* avatar, void* action) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov edi, ebp
        sub esp, 0a0h
        lea ebp, [esp+98h]
        mov eax, [edi+8]
        mov [ebp-14h], eax
        mov eax, [edi+0ch]
        mov [ebp-98h], eax
        mov ebx, [edi+10h]
        mov esi, [edi+14h]
        xor ecx, ecx
        mov eax, 10955c2dh
        call eax
        mov ebp, edi
        lea esp, [ebp-0ch]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
__declspec(naked) int QueuedAt(int skill, int index, int base) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov edi, ebp
        sub esp, 80h
        lea ebp, [esp+40h]
        mov ebx, [edi+8]
        mov eax, [edi+0ch]
        mov [ebp+24h], eax
        mov eax, [edi+10h]
        mov [ebp+10h], eax
        mov eax, 1066b0cbh
        call eax
        mov ebp, edi
        lea esp, [ebp-0ch]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

__declspec(naked) DWORD PrepareCells(void* bank, int index, void** first) {
    __asm {
        push ebx
        mov eax,[esp+8]
        mov ebx,[esp+0ch]
        mov edx,10454564h
        call edx
        mov edx,[esp+10h]
        mov [edx],ecx
        pop ebx
        ret
    }
}
__declspec(naked) DWORD UpdateCells(void* bank, int index, void** first) {
    __asm {
        push ebp
        push esi
        push edi
        sub esp,30h
        lea ebp,[esp+28h]
        mov esi,[esp+40h]
        mov edi,[esp+44h]
        mov eax,1045279ch
        call eax
        mov edx,[esp+48h]
        mov ecx,[ebp-24h]
        mov [edx],ecx
        add esp,30h
        pop edi
        pop esi
        pop ebp
        ret
    }
}
__declspec(naked) void ResetCells(void* avatar,int bank) {
    __asm {
        push esi
        mov ecx,[esp+8]
        mov eax,[esp+0ch]
        imul eax,5dch
        mov edx,10453a38h
        call edx
        pop esi
        ret
    }
}
// Execute the native metadata-loader comparison, not just action name lookup.
__declspec(naked) int LoadsAction(int index) {
    __asm {
        push esi
        mov esi,[esp+8]
        mov eax,104073a2h
        call eax
        setl al
        movzx eax,al
        pop esi
        ret
    }
}
__declspec(naked) int LoadsResource(int offset) {
    __asm {
        push ebp
        mov ebp,esp
        sub esp,30h
        mov eax,[ebp+8]
        mov [ebp-30h],eax
        mov eax,1040b2a2h
        call eax
        setl al
        movzx eax,al
        mov esp,ebp
        pop ebp
        ret
    }
}
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    // Reserve native addresses before loading the large PE file into a heap buffer.
    for(DWORD page:{0x10400000,0x10410000,0x10450000,0x104a0000,0x104e0000,0x104f0000,0x10640000,0x10660000,0x10750000,0x10760000,0x10920000,0x10950000,0x10960000,0x10970000,0x10980000,0x10a00000})
        if(VirtualAlloc(reinterpret_cast<void*>(page),0x10000,MEM_RESERVE|MEM_COMMIT,
                PAGE_EXECUTE_READWRITE)!=reinterpret_cast<void*>(page)) { printf("Mapping failed at %08lx error %lu\n",page,GetLastError()); return 3; }

    std::ifstream file(argv[1],std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
    if(bytes.size()<4096)return 2;
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(bytes.data()+dos->e_lfanew);
    auto section=IMAGE_FIRST_SECTION(nt);
    for(DWORD page:{0x10400000,0x10410000,0x10450000,0x104a0000,0x104e0000,0x104f0000,0x10640000,0x10660000,0x10750000,0x10760000,0x10920000,0x10950000,0x10960000,0x10970000,0x10980000,0x10a00000}) {
        BYTE* image=reinterpret_cast<BYTE*>(page);
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
            const DWORD rva=page-0x10400000;
            const DWORD begin=(std::max)(rva,section[i].VirtualAddress);
            const DWORD end=(std::min)(rva+0x10000,section[i].VirtualAddress+section[i].SizeOfRawData);
            if(begin<end) memcpy(image+begin-rva,bytes.data()+section[i].PointerToRawData+begin-section[i].VirtualAddress,end-begin);
        }
    }
    BYTE original=*reinterpret_cast<BYTE*>(0x10955edd);
    *reinterpret_cast<BYTE*>(0x10955edd)=0;
    if(EvanRuntime::Install() || *reinterpret_cast<BYTE*>(0x10955e26)!=0x0f) return 4;
    *reinterpret_cast<BYTE*>(0x10955edd)=original;
    if(!EvanRuntime::Install())return 5;
    // Appended action tables keep all existing indexes and extend only the
    // name lookup, avatar bounds, and dynamically allocated dragon caches.
    const DWORD avatarTable=*reinterpret_cast<DWORD*>(0x10406c1c);
    const DWORD dragonTable=*reinterpret_cast<DWORD*>(0x10411494);
    if(!avatarTable || !dragonTable || avatarTable==0xbec620 || dragonTable==0xbec3f8)return 21;
    if(*reinterpret_cast<DWORD*>(0x104a8d33)!=avatarTable+163*24)return 22;
    if(*reinterpret_cast<DWORD*>(0x104a8dc5)!=dragonTable+22*4)return 23;
    if(*reinterpret_cast<BYTE*>(0x10453b2d)!=163 || *reinterpret_cast<BYTE*>(0x104fec49)!=22)return 24;

    // Regression for the 2026-09-21 null-frame crash at native 00455F53:
    // both loader loops must reach index 162, but stop before index 163.
    *reinterpret_cast<BYTE*>(0x104073a8)=0xc3;
    *reinterpret_cast<BYTE*>(0x1040b2a9)=0xc3;
    for(int index:{0,161,162,163}) {
        if(LoadsAction(index)!=(index<163))return 35;
        if(LoadsResource(index*48)!=(index<163))return 36;
    }
    if(*reinterpret_cast<DWORD*>(0x1040acdb)!=163 ||
       *reinterpret_cast<DWORD*>(0x1040acff)!=163*16)return 37;

    // Exercise the installed cache trampolines, both banks and destruction.
    *reinterpret_cast<BYTE*>(0x1045456f)=0xc3;
    *reinterpret_cast<BYTE*>(0x104527aa)=0xc3;
    *reinterpret_cast<BYTE*>(0x10453a3f)=0xc3;
    *reinterpret_cast<BYTE*>(0x10450098)=0xc3;
    int destroyed=0;
    BYTE destructor[]={0xff,0x05,0,0,0,0,0xc3};
    *reinterpret_cast<int**>(destructor+2)=&destroyed;
    memcpy(reinterpret_cast<void*>(0x10457235),destructor,sizeof(destructor));
    memcpy(reinterpret_cast<void*>(0x10457240),destructor,sizeof(destructor));
    BYTE owner[0x1200]{};
    void *normal=nullptr,*updated=nullptr;
    DWORD cell=PrepareCells(owner+0x4f0,162,&normal);
    if(!normal || cell!=reinterpret_cast<DWORD>(normal)+4)return 26;
    if(UpdateCells(owner+0x4f0,162,&updated)!=cell || normal!=updated)return 27;
    void* second=nullptr;
    PrepareCells(owner+0x4f0+0x5dc,162,&second);
    if(normal==second)return 28;
    for(int index:{0,40,139,161}) {
        if(PrepareCells(owner+0x4f0,index,&normal)!=reinterpret_cast<DWORD>(owner+0x4f0+0x298+index*4))return 29;
        if(normal!=owner+0x4f0+0x10+index*4)return 30;
        if(UpdateCells(owner+0x4f0,index,&updated)!=reinterpret_cast<DWORD>(owner+0x4f0+0x298+index*4) || normal!=updated)return 31;
    }
    ResetCells(owner,0);
    if(destroyed!=2)return 32;
    ResetCells(owner,0);
    if(destroyed!=2)return 33;
    reinterpret_cast<void(__thiscall*)(void*)>(0x10450093)(owner+0x4f0+0x5dc);
    if(destroyed!=4)return 34;
    // Execute the new gate, including the predecessor comparison flags.
    const BYTE yes[]={0xb8,1,0,0,0,0xc3},no[]={0x31,0xc0,0xc3};
    memcpy(reinterpret_cast<void*>(0x10956372),yes,sizeof(yes));
    memcpy(reinterpret_cast<void*>(0x10955e2c),no,sizeof(no));
    for(int skill:{22151002,22171003,22181001,22001001}) {
        // CompareRegister sets EAX; a preceding cmp is reproduced below.
        const BYTE gate[]={0x8b,0x44,0x24,4,0x3d,0x7b,0x4d,0x52,1,0xe9,0,0,0,0};
        BYTE* thunk=reinterpret_cast<BYTE*>(0x10958000);
        memcpy(thunk,gate,sizeof(gate));
        *reinterpret_cast<DWORD*>(thunk+10)=0x10955e26-(0x10958000+14);
        if(reinterpret_cast<int(__cdecl*)(int)>(thunk)(skill)!=(skill==22151002 || skill==22171003))return 25;
    }
    // Execute the native skill gate, stack-based hit offsets and queue hook.
    // Stop before secure storage; return the actual scheduled timestamp in ECX.
    const BYTE returnTime[]={0x8b,0xc1,0xc3};
    memcpy(reinterpret_cast<void*>(0x1066b101),returnTime,sizeof(returnTime));
    memcpy(reinterpret_cast<void*>(0x1066b13c),returnTime,sizeof(returnTime));
    const int offsets[]={0,24,72,168,0};
    for(int base:{0,948,100000}) {
        for(int hit=0;hit<5;++hit) {
            if(QueuedAt(22171002,hit,base)!=base+offsets[hit])return 16;
            if(QueuedAt(2121006,hit,base)!=base+120*hit)return 17;
            if(QueuedAt(3111006,hit,base)!=base+60*hit)return 18;
        }
    }
    const BYTE durationContinuation=*reinterpret_cast<BYTE*>(0x10955c34);
    *reinterpret_cast<BYTE*>(0x10955c34)=0xc3;
    int avatar[0x600 / 4]{};
    int action[6]{};
    int frames[1 + 10 * 8]{};
    frames[0]=10;
    const int delays[]={270,90,630,90,90,60,60,240,60,420};
    for(int i=0;i<10;++i)frames[1+i*8+2]=delays[i];
    *reinterpret_cast<int**>(&action[5])=&frames[1];
    for(int stale:{0,90,2430,10000}) {
        avatar[0x4f8 / 4]=stale;
        for(auto timing: {std::pair<int,int>{0,1505},{2,1505},{4,1754},{6,2010},{10,2510},{12,2510}}) {
            if(DurationAt(22171002,timing.first,avatar,action)!=timing.second)return 12;
            for(int skill:{22171003,22181001,2121006})
                if(DurationAt(skill,timing.first,avatar,action)!=stale)return 13;
        }
    }
    // Missing action data must fall back, without dereferencing a null array.
    action[5]=0;
    if(DurationAt(22171002,6,avatar,action)!=10000)return 14;
    // Also execute the original imul/cdq/idiv that derives the hit timestamp.
    // A preceding 10-second action formerly yielded 5820ms for this skill;
    // the native current-action result must be 1170ms, or 1020ms at speed 4.
    *reinterpret_cast<int**>(&action[5])=&frames[1];
    action[3]=2010;
    action[4]=1170;
    *reinterpret_cast<BYTE*>(0x10955c34)=durationContinuation;
    *reinterpret_cast<BYTE*>(0x10955c3c)=0xc3;
    if(DurationAt(22171002,6,avatar,action)!=1170 ||
       DurationAt(22171002,4,avatar,action)!=1020 ||
       DurationAt(22171003,6,avatar,action)!=5820)return 15;
    const int fastDelays[]={112,32,256,32,32,32,16,96,16,176};
    for(int i=0;i<10;++i)frames[1+i*8+2]=fastDelays[i];
    action[3]=800;action[4]=464;
    if(DurationAt(22171002,6,avatar,action)!=464 ||
       DurationAt(22171002,3,avatar,action)!=377)return 19;
    *reinterpret_cast<BYTE*>(0x10955c34)=0xc3;
    if(DurationAt(22171002,6,avatar,action)!=800 ||
       DurationAt(22171002,3,avatar,action)!=650)return 20;
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
    if(*reinterpret_cast<DWORD*>(0x10955e22)!=22171003 || *reinterpret_cast<BYTE*>(0x10955e26)!=0xe9)return 7;
    *reinterpret_cast<BYTE*>(0x10955e26)=0xc3;
    for(DWORD skill:{22171003,22181001,22171002,2121006})
        if(((CompareRegister(0x10955e21,skill)&0x40)!=0)!=(skill==22171003))return 11;
    puts("PASS native image signatures, atomic rejection, Illusion timing independent of previous action across booster speeds, unrelated skills unchanged, Blaze/Flame Wheel gates, mastery and cash books");
    for(DWORD page:{0x104e0000,0x104f0000,0x10750000,0x10760000,0x10950000,0x10960000,0x10a00000}) VirtualFree(reinterpret_cast<void*>(page),0,MEM_RELEASE);
    return 0;
}
