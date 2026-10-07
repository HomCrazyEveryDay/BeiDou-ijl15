#include "../ezorsia/stdafx.h"
#include "../ezorsia/EquipmentCritical.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>

static void Require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void Update(int probability, int damage = 0) {
    unsigned char packet[]{0,0,0,0,0x10,0x10,1,static_cast<unsigned char>(probability),
        static_cast<unsigned char>(damage),static_cast<unsigned char>(damage >> 8)};
    Require(EquipmentCritical::HandlePacket(packet, sizeof(packet)), "snapshot consumed");
}
__declspec(naked) unsigned Run(DWORD address, int* frame, void* target = nullptr) {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov esi, [esp+14h]
        mov ebp, [esp+18h]
        mov edi, [esp+1ch]
        xor eax, eax
        xor ecx, ecx
        call esi
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
static unsigned __fastcall NativeFooter(void*, void*) { return 0x12345678; }
static void Jump(DWORD address, void* function) {
    auto* code = reinterpret_cast<unsigned char*>(address);
    code[0] = 0xe9;
    *reinterpret_cast<DWORD*>(code+1) = reinterpret_cast<DWORD>(function) - address - 5;
}

int main(int argc, char** argv) {
    Require(argc == 2, "original EXE path");
    FILE* file = nullptr;
    Require(fopen_s(&file, argv[1], "rb") == 0, "read EXE");
    fseek(file,0,SEEK_END); std::vector<unsigned char> bytes(ftell(file)); rewind(file);
    Require(fread(bytes.data(),1,bytes.size(),file) == bytes.size(), "read complete EXE"); fclose(file);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    Require(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.ImageBase == 0x400000, "GMS083");
    auto* mapped = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(0x10400000),
        nt->OptionalHeader.SizeOfImage, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Require(mapped != nullptr, "isolated image mapping");
    auto* sections = IMAGE_FIRST_SECTION(nt);
    for (int i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(mapped+sections[i].VirtualAddress,bytes.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    // Relocate only absolute constant operands in the original x87 instructions.
    for (DWORD instruction : {0x1079017c,0x107901a9,0x10791daf,0x10791dee,0x10791df4})
        *reinterpret_cast<DWORD*>(instruction+2) += 0x10000000;
    auto* site = reinterpret_cast<unsigned char*>(0x1078e367);
    site[1] ^= 1;
    Require(!EquipmentCritical::Install() && *reinterpret_cast<unsigned char*>(0x1079184e)==0x8b,
        "signature failure installs no partial hooks");
    site[1] ^= 1;
    Require(EquipmentCritical::Install() && EquipmentCritical::Install(),"install is idempotent");
    for (DWORD stop : {0x1078e370,0x10791853,0x107901b8,0x10791e00,0x10952b3d})
        *reinterpret_cast<unsigned char*>(stop)=0xc3;
    Jump(0x106711ac,reinterpret_cast<void*>(&NativeFooter));
    FlushInstructionCache(GetCurrentProcess(),mapped,nt->OptionalHeader.SizeOfImage);

    for (bool magic : {false,true}) {
        for (int chance : {0,5,15,85,100}) {
            Update(chance);
            int storage[256]{}; int* frame=storage+128;
            int& probability=frame[magic ? 0x0c/4 : -0x40/4];
            int& damage=frame[magic ? -0x24/4 : -0x34/4];
            Run(magic ? 0x1079184e : 0x1078e367,frame);
            Require(probability==chance && damage==(chance ? 150 : 0),"real hook grants correct chance and baseline");
            if (!magic && chance) Require(frame[-0x80/4]==1,"equipment enables native physical critical gate");
            int count=0;
            for (int roll=0;roll<100;++roll) {
                int flags=0, placeholder=0;
                frame[-1]=0;
                frame[magic ? -0x8c/4 : -0x64/4]=roll*100000+50000;
                double& total=*reinterpret_cast<double*>(frame+(magic ? -0x0c/4 : -0x20/4));
                total=1000;
                if (magic) {
                    frame[0x40/4]=reinterpret_cast<int>(&placeholder);
                    frame[0x34/4]=reinterpret_cast<int>(&flags)-reinterpret_cast<int>(&placeholder);
                } else {
                    frame[-0x08/4]=1000;
                    frame[0x44/4]=reinterpret_cast<int>(&flags);
                    frame[0x24/4]=0;
                }
                Run(magic ? 0x10791d80 : 0x10790113,frame);
                Require(flags==(roll<chance),"actual native RNG and flag match probability");
                Require(std::abs(total-(flags ? 1500 : 1000))<0.001,"actual native critical damage is applied once");
                count+=flags;
            }
            Require(count==chance,"5 percentage points produce exactly five of 100 stratified rolls");
        }
    }
    for (bool magic : {false,true}) for (int nativeChance : {0,40,95,100}) {
        for (int nativeDamage : {0,150,200,250,340}) {
            Update(5,10);
            int storage[256]{};int* frame=storage+128;
            int& chance=frame[magic ? 0x0c/4 : -0x40/4];
            int& damage=frame[magic ? -0x24/4 : -0x34/4];
            chance=nativeChance;damage=nativeDamage;
            Run(magic ? 0x1079184e : 0x1078e367,frame);
            Require(chance==(nativeChance<95 ? nativeChance+5 : 100),"passive and sharp-eyes stack with cap");
            const int expected=(!nativeDamage ? 50 : magic ? nativeDamage-(nativeDamage>200 ? 200 : 100) : nativeDamage-100)+10;
            const int actual=magic ? damage-(damage>200 ? 200 : 100) : damage-100;
            Require(actual==expected,"damage addition preserves magic threshold encoding");
        }
    }
    Update(0,10);int chance=0,damage=0;EquipmentCritical::Add(chance,damage,false);
    Require(chance==0 && damage==0,"damage-only gear cannot create chance");
    Update(5);
    unsigned target[0x98/4]{};int storage[256]{};int* frame=storage+128;
    target[0]=0x1234;
    for (int lines : {1,4,15}) {
        frame[-0x58/4]=lines;
        for(unsigned mask : {0u,1u,5u,0x7fffu}) {
            for(int i=0;i<15;++i) {target[6+i]=65000+i;target[21+i]=(mask>>i)&1;}
            Require(Run(0x10952b36,frame,target)==(0xcc010000|(mask&((1u<<lines)-1))),"actual melee footer carries all critical flags");
            for(int i=0;i<15;++i)Require(target[6+i]==65000+i,"footer leaves damage magnitudes intact");
        }
    }
    Update(0);Require(Run(0x10952b36,frame,target)==0x12345678,"unequip restores native footer");
    for(int size=6;size<=11;++size) {
        Update(5);
        unsigned char bad[11]{0,0,0,0,0x10,0x10,2,101,0xff,0xff,0};
        Require(EquipmentCritical::HandlePacket(bad,size) && !EquipmentCritical::Active(),"malformed snapshot clears state");
    }
    Update(5);unsigned char login[6]{};EquipmentCritical::HandlePacket(login,6);
    Require(!EquipmentCritical::Active(),"new login clears prior character's state");
    puts("PASS: native physical/magic chance and damage, 5%, stacking/cap, equipment removal, melee masks, packet validation and signature guards");
}
