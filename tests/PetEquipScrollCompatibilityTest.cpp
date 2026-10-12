#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../ezorsia/PetEquipScrollCompatibility.h"

namespace CrashReporter { void RecordEvent(const char*,const char*,...) {} }
static void Require(bool ok, const char* message) {
    if (!ok) { printf("FAIL %s\n",message); exit(1); }
}

// Execute just the real, self-contained EXE category predicate in an isolated
// test process, without launching the game or executing its entry point.
static BYTE* MapClient(const char* path) {
    FILE* file = nullptr;
    Require(!fopen_s(&file,path,"rb"),"open client EXE");
    fseek(file,0,SEEK_END);
    std::vector<BYTE> bytes(ftell(file));
    rewind(file);
    Require(fread(bytes.data(),1,bytes.size(),file)==bytes.size(),"read client EXE");
    fclose(file);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    auto* base = static_cast<BYTE*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,
        MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Require(base!=nullptr,"map client EXE");
    memcpy(base,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(base+section[i].VirtualAddress,bytes.data()+section[i].PointerToRawData,section[i].SizeOfRawData);
    return base;
}

int main(int argc, char** argv) {
    Require(argc==2,"client path required");
    BYTE* base = MapClient(argv[1]);
    using CanScroll = int(__cdecl*)(int,int);
    const auto canScroll = reinterpret_cast<CanScroll>(base+0xf5497);
    for (int outfit : {1802896,1802897,1802899}) {
        Require(!canScroll(2049100,outfit),"reproduce native chaos rejection");
        Require(!canScroll(2049115,outfit),"reproduce native positive chaos rejection");
    }

    // Compare every 204 scroll ID with the original predicate, including nearby
    // outfits, non-outfit pet items, every ordinary equipment category, and
    // invalid non-equipment IDs. Only the six requested pairs may change.
    std::vector<int> equipment = {1802895,1802896,1802897,1802898,1802899,1802900,
        1802000,1802001,1812000,1822000,1832000,5002414,2049100,0};
    for (int category=100;category<=170;++category) equipment.push_back(category*10000+2000);
    std::vector<int> original;
    for (int scroll=2040000;scroll<2050000;++scroll)
        for (int equip : equipment) original.push_back(canScroll(scroll,equip));

    Require(!PetEquipScrollCompatibility::Install(nullptr),"null image rejected");
    const BYTE saved = base[0xf54fe];
    base[0xf54fe] ^= 1;
    Require(!PetEquipScrollCompatibility::Install(reinterpret_cast<HMODULE>(base)),"changed signature rejected");
    Require(base[0xf54fe]==(saved^1),"rejected image left untouched");
    base[0xf54fe] = saved;
    Require(PetEquipScrollCompatibility::Install(reinterpret_cast<HMODULE>(base)),"hook installed");
    Require(PetEquipScrollCompatibility::Install(reinterpret_cast<HMODULE>(base)),"same image install is idempotent");
    Require(!PetEquipScrollCompatibility::Install(nullptr),"different image rejected after install");

    size_t index = 0, changes = 0;
    for (int scroll=2040000;scroll<2050000;++scroll) {
        for (int equip : equipment) {
            const int result = canScroll(scroll,equip);
            const bool requested = (scroll==2049100 || scroll==2049115)
                && (equip==1802896 || equip==1802897 || equip==1802899);
            if (result != (requested ? 1 : original[index])) {
                printf("FAIL scroll=%d equip=%d before=%d after=%d\n",scroll,equip,original[index],result);
                return 1;
            }
            changes += result!=original[index++];
        }
    }
    Require(changes==6,"only the six requested pairs change");
    printf("PASS: native rejection reproduced; %zu category pairs verified, exactly %zu corrected.\n",index,changes);
    VirtualFree(base,0,MEM_RELEASE);
}
