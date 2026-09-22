#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

// Execute the original CMob::Add/Remove beacon instructions. Only their list
// allocator and clock dependencies are replaced; no client process is started.
static std::map<void*,std::vector<DWORD*>> lists;
static DWORD* __fastcall Append(void* list,void*) {
    auto record=new DWORD[4]{};
    lists[list].push_back(record);
    *reinterpret_cast<DWORD**>(static_cast<BYTE*>(list)+0x10)=record;
    return record;
}
static void __fastcall Remove(void* list,void*,DWORD* record) {
    auto& entries=lists[list];
    auto it=std::find(entries.begin(),entries.end(),record);
    if(it==entries.end())std::abort();
    entries.erase(it);
    delete[] record;
}
static void Jump(DWORD site,void* target) {
    auto p=reinterpret_cast<BYTE*>(site);p[0]=0xe9;
    *reinterpret_cast<DWORD*>(p+1)=reinterpret_cast<DWORD>(target)-(site+5);
}
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    for(DWORD page:{0x10670000,0x10980000})
        if(!VirtualAlloc(reinterpret_cast<void*>(page),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE))return 2;
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
    if(bytes.size()<4096)return 3;
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(bytes.data()+dos->e_lfanew);
    auto sections=IMAGE_FIRST_SECTION(nt);
    for(DWORD page:{0x10670000,0x10980000})for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
        DWORD rva=page-0x10400000;
        DWORD begin=(std::max)(rva,sections[i].VirtualAddress);
        DWORD end=(std::min)(rva+0x10000,sections[i].VirtualAddress+sections[i].SizeOfRawData);
        if(begin<end)memcpy(reinterpret_cast<BYTE*>(page)+begin-rva,
            bytes.data()+sections[i].PointerToRawData+begin-sections[i].VirtualAddress,end-begin);
    }
    Jump(0x10672b51,reinterpret_cast<void*>(&Append));
    Jump(0x10672b8f,reinterpret_cast<void*>(&Remove));
    const BYTE time[]={0xb8,0xe8,0x03,0,0,0xc3};
    memcpy(reinterpret_cast<void*>(0x10987257),time,sizeof(time));
    auto add=reinterpret_cast<void(__thiscall*)(void*,int,int)>(0x10671121);
    auto cancel=reinterpret_cast<void(__thiscall*)(void*)>(0x10671166);
    DWORD mobs[8][0x120/4]{};
    auto count=[&](int i){return lists[reinterpret_cast<BYTE*>(mobs[i])+0xb0].size();};
    // Reproduce the user's repeated A,A,A then B: a single cancel leaves two
    // unreachable marker records. Sending more cancels cannot clear them.
    for(int i=0;i<3;++i)add(mobs[0],22151002,0);
    if(count(0)!=3)return 4;
    cancel(mobs[0]);
    if(count(0)!=2 || mobs[0][0x114/4]!=0)return 5;
    cancel(mobs[0]);
    if(count(0)!=2)return 6;
    // Start a clean map, then use cancel-before-every-give, including repeats.
    for(auto p:lists[reinterpret_cast<BYTE*>(mobs[0])+0xb0])delete[] p;
    lists.clear();memset(mobs,0,sizeof(mobs));
    int previous=-1;
    for(int next:{0,0,0,1,2,3,3,4,4,5,6,0,0,7}) {
        if(previous>=0)cancel(mobs[previous]);
        add(mobs[next],22151002,0);
        for(int i=0;i<8;++i)if(count(i)!=(i==next?1u:0u))return 7;
        previous=next;
    }
    cancel(mobs[previous]);
    for(int i=0;i<8;++i)if(count(i)!=0)return 8;
    puts("PASS native repeated-mark leak reproduced; cancel-before-every-give keeps exactly one marker across eight targets; final cancel clears it");
    return 0;
}
