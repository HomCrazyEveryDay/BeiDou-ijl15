#include "../ezorsia/VisitorsMorph.cpp"
#include <cstdarg>
#include <cstdlib>
#include <vector>

namespace {
void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int logCount, signatureLogs, currentJob, castCount, fallbackCount, actionCount, meleeCount;
int lastSkill, lastScan, lastLevel;
void* lastResult;
void* lastUser;
bool missingResource;
unsigned char user[0x600]{}, otherUser[0x600]{}, context[0x20C0]{}, character[0x100]{};
unsigned char levelData[0xE8]{};
void* methods[17]{};
int entries[] = {1066,1067,10001066,10001067,20001066,20001067,20011066,20011067};
void* database = reinterpret_cast<void*>(0x12345678);
std::vector<unsigned char> reactorPacket;

template<typename Value> void Set(void* object, size_t offset, Value value) {
    *reinterpret_cast<Value*>(static_cast<unsigned char*>(object) + offset) = value;
}
void Jump(DWORD address, void* target) {
    auto* code = reinterpret_cast<unsigned char*>(address);
    code[0] = 0xE9;
    *reinterpret_cast<DWORD*>(code + 1) = reinterpret_cast<DWORD>(target) - address - 5;
}
void* Target(DWORD address) {
    auto* code = reinterpret_cast<unsigned char*>(Native(address));
    Require(code[0] == 0xE8 || code[0] == 0xE9, "installed relative branch");
    return code + 5 + *reinterpret_cast<int*>(code + 1);
}
int __fastcall Job(void* instance, void*) {
    Require(instance == user || instance == otherUser, "job receiver");
    return currentJob;
}
void* __fastcall Resource(void* instance, void*, int skill) {
    Require(instance == database, "skill database receiver");
    for (auto& entry : entries) if (entry == skill) return missingResource ? nullptr : &entry;
    Require(false, "only Visitors use synthetic resource lookup");
    return nullptr;
}
int __fastcall Cast(void* instance, void*, int skill, int scan, void* result) {
    ++castCount; lastUser = instance; lastSkill = skill; lastScan = scan; lastResult = result;
    return 73;
}
int __fastcall NativeLevelFallback(void* instance, void*, void* data, int skill, void** entry) {
    Require(instance == database && data, "native level fallback ABI");
    ++fallbackCount; lastSkill = skill;
    if (entry) *entry = reinterpret_cast<void*>(0x7777);
    return 23;
}
__declspec(naked) void LevelFallback() {
    __asm {
        push dword ptr [ebp+10h]
        push dword ptr [ebp+0ch]
        push dword ptr [ebp+8]
        mov ecx, [ebp-4]
        call NativeLevelFallback
        leave
        ret 0ch
    }
}
int __cdecl NativeAction(int category, int weapon, void* entry, int level, unsigned random) {
    Require(category == 9 && weapon == 44 && level == 2 && random == 0x1234, "action fallback ABI");
    ++actionCount;
    return 122;
}
int __cdecl SafeInt(const void* field, unsigned checksum) {
    Require(field == levelData + 0xDC && checksum == 0x89ABCDEF, "fixdamage safe integer layout");
    return *static_cast<const int*>(field);
}
int __fastcall NativeMorph(void* avatar, void*) {
    Require(avatar == user + 0x88, "native morph receiver unchanged");
    ++fallbackCount;
    return 19;
}
int __fastcall Melee(void* instance, void*, void* entry, int level) {
    Require(instance == user && entry, "native melee wrapper ABI");
    ++meleeCount; lastLevel = level; lastSkill = *static_cast<int*>(entry);
    return 81;
}
__declspec(naked) void Accepted() {
    __asm {
        mov eax, 101
        ret
    }
}
__declspec(naked) void Rejected() {
    __asm {
        mov eax, 102
        ret
    }
}
__declspec(naked) void ConditionsReturn() {
    __asm {
        pop edi
        pop esi
        pop ebx
        leave
        ret 10h
    }
}
void __fastcall PacketStart(void*, void*, unsigned opcode) {
    reactorPacket.clear();
    reactorPacket.push_back(static_cast<unsigned char>(opcode));
    reactorPacket.push_back(static_cast<unsigned char>(opcode >> 8));
}
void __fastcall EncodeFour(void*, void*, unsigned value) {
    for (int shift=0; shift<32; shift+=8)
        reactorPacket.push_back(static_cast<unsigned char>(value >> shift));
}
void __fastcall EncodeTwo(void*, void*, unsigned value) {
    reactorPacket.push_back(static_cast<unsigned char>(value));
    reactorPacket.push_back(static_cast<unsigned char>(value >> 8));
}
__declspec(naked) void StoreDamage() {
    __asm {
        mov ecx, [ebp-48h]
        mov [ecx], eax
        ret
    }
}
__declspec(naked) void DamageFallback() {
    __asm {
        pop eax
        cmp eax, 89abcdefh
        je valid
        int 3
    valid:
        mov eax, 103
        ret
    }
}
struct Registers { DWORD eax, ebx, ecx, edx, esi, edi, ebp, flags; };
__declspec(naked) void Invoke(void* gate, Registers* state) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        push dword ptr [ebp+0ch]
        push ebp
        push dword ptr [ebp+8]
        mov eax, [ebp+0ch]
        push dword ptr [eax+1ch]
        popfd
        mov ebx, [eax+4]
        mov ecx, [eax+8]
        mov edx, [eax+0ch]
        mov esi, [eax+10h]
        mov edi, [eax+14h]
        mov ebp, [eax+18h]
        mov eax, [eax]
        call dword ptr [esp]
        pushfd
        pushad
        mov eax, [esp+2ch]
        mov edx, [esp+1ch]
        mov [eax], edx
        mov edx, [esp+10h]
        mov [eax+4], edx
        mov edx, [esp+18h]
        mov [eax+8], edx
        mov edx, [esp+14h]
        mov [eax+0ch], edx
        mov edx, [esp+4]
        mov [eax+10h], edx
        mov edx, [esp]
        mov [eax+14h], edx
        mov edx, [esp+8]
        mov [eax+18h], edx
        mov edx, [esp+20h]
        mov [eax+1ch], edx
        popad
        popfd
        add esp, 4
        pop ebp
        add esp, 4
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

Registers State(int* frame) {
    return {0x1111,0x2222,0x3333,0x4444,0x5555,0x6666,reinterpret_cast<DWORD>(frame),0x202};
}
void* LoadImage(const char* filename) {
    FILE* file = nullptr;
    Require(fopen_s(&file, filename, "rb") == 0, "read original EXE without launching");
    fseek(file, 0, SEEK_END);
    std::vector<unsigned char> bytes(ftell(file));
    rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read complete EXE");
    fclose(file);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    Require(image->Signature == IMAGE_NT_SIGNATURE && image->OptionalHeader.ImageBase == 0x400000, "GMS083 PE");
    auto* mapped = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(Native(0x00400000)),
        image->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Require(mapped != nullptr, "isolated image mapping");
    std::memcpy(mapped, bytes.data(), image->OptionalHeader.SizeOfHeaders);
    const auto* sections = IMAGE_FIRST_SECTION(image);
    for (unsigned index = 0; index < image->FileHeader.NumberOfSections; ++index)
        std::memcpy(mapped + sections[index].VirtualAddress, bytes.data() + sections[index].PointerToRawData,
            sections[index].SizeOfRawData);
    return mapped;
}

void InstallTests(void* image) {
    Require(!VisitorsMorph::Install(nullptr), "wrong image rejected");
    for (const auto& changed : patches) {
        auto* site = reinterpret_cast<unsigned char*>(changed.address);
        Require(std::memcmp(site, changed.expected, changed.size) == 0, "patch baseline matches disk EXE");
        site[0] ^= 1;
        const int previousLogs = signatureLogs;
        Require(!VisitorsMorph::Install(static_cast<HMODULE>(image)), "signature mismatch rejects whole install");
        Require(signatureLogs == previousLogs + 1, "signature failure logged");
        site[0] ^= 1;
        for (const auto& original : patches)
            Require(std::memcmp(reinterpret_cast<void*>(original.address), original.expected, original.size) == 0,
                "no partial installation after mismatch");
    }
    const unsigned char evanLevel[] = {0x90,0x90,0x90,0x90,0x90,0x90};
    std::memcpy(reinterpret_cast<void*>(Native(0x00761717)), evanLevel, 6);
    std::memcpy(reinterpret_cast<void*>(Native(0x00761723)), evanLevel, 6);
    Require(VisitorsMorph::Install(static_cast<HMODULE>(image)), "install beside Evan level patches");
    Require(VisitorsMorph::Install(static_cast<HMODULE>(image)), "idempotent installation");
    Require(std::memcmp(reinterpret_cast<void*>(Native(0x00761717)), evanLevel, 6) == 0, "Evan patch retained");
    for (const auto& patch : patches) {
        Require(Target(patch.address - 0x10400000 + 0x400000) == patch.target, "patch destination");
        Require(*reinterpret_cast<unsigned char*>(patch.address) == patch.opcode, "CALL versus JMP ABI");
    }
}

void LevelsAndInputs() {
    using Level = int(__fastcall*)(void*, void*, void*, int, void**);
    using Key = int(__fastcall*)(void*, void*, int, int, void*);
    const auto query = reinterpret_cast<Level>(Target(0x007616F6));
    const auto casting = reinterpret_cast<Level>(Target(0x009675D5));
    const auto key = reinterpret_cast<Key>(Target(0x0094FA28));
    const int jobs[] = {0,112,232,522,900,1000,1112,1512,2000,2100,2112,2001,2200,2218};
    for (int job : jobs) {
        currentJob = job;
        const int prefix = job == 2001 || job / 100 == 22 ? 20010000 : (job / 1000) * 10000000;
        for (int morph = 64; morph <= 67; ++morph) {
            Set(user, 0x528, morph);
            Require(InputSkill(user,1066) == prefix+1066 && InputSkill(user,1067) == prefix+1067, "092 job prefix");
            for (int baseSkill : {1066,1067}) {
                const int skill = prefix + baseSkill;
                void* entry = nullptr;
                Require(query(database,nullptr,character,skill,&entry) == 1 && entry && *static_cast<int*>(entry) == skill,
                    "ordinary lookup synthesizes level one and entry");
                Require(query(database,nullptr,character,skill,nullptr) == 1, "optional entry out");
                Require(casting(database,nullptr,character,skill,&entry) == morph-63, "cast level follows morph");
                Require(VisitorAction(user,entry,morph-63) == (baseSkill == 1066 ? 16 : 17), "stab action");
                Require(ChooseAction(0,0,entry,morph-63,0) == (baseSkill == 1066 ? 16 : 17), "unarmed action override");
            }
            Require(key(user,nullptr,2321008,77,reinterpret_cast<void*>(9)) == 73, "key cast result");
            Require(lastUser == user && lastSkill == prefix+1067 && lastScan == 0 && lastResult == nullptr,
                "skill key replacement uses 092 arguments");
            int frame[96]{};
            auto registers = State(frame+48);
            registers.esi = reinterpret_cast<DWORD>(user);
            const int previous = castCount;
            Invoke(Target(0x0094E2B3),&registers);
            Require(castCount == previous+1 && lastSkill == prefix+1066 && lastScan == 0 && !lastResult,
                "ordinary attack replacement");
            Require(registers.eax == 101 && registers.ebx == 0x2222 && registers.esi == reinterpret_cast<DWORD>(user),
                "normal input saves registers and exits original frame");
        }
    }
    currentJob=112;
    for (int morph : {0,1,63,68,1000,1003,1100,1103}) {
        Set(user,0x528,morph);
        Require(query(database,nullptr,character,1066,nullptr) == 23, "other morph original level lookup");
        Require(key(user,nullptr,1121008,42,reinterpret_cast<void*>(7)) == 73 && lastSkill == 1121008
            && lastScan == 42 && lastResult == reinterpret_cast<void*>(7), "other morph original key arguments");
        int frame[96]{};
        auto registers=State(frame+48);
        registers.esi=reinterpret_cast<DWORD>(user);
        const int previous=castCount;
        Invoke(Target(0x0094E2B3),&registers);
        Require(castCount == previous && registers.edi == reinterpret_cast<DWORD>(user+0x88), "normal input fallback LEA");
    }
    Set(user,0x528,64);
    Require(query(database,nullptr,otherUser,1066,nullptr) == 23, "other character not synthesized");
    Require(query(database,nullptr,character,10001066,nullptr) == 23, "wrong job prefix not synthesized");
    Require(query(database,nullptr,character,1121008,nullptr) == 23, "other skill not synthesized");
    currentJob=3000;
    Require(InputSkill(user,1066) == 0, "unsupported 3000 family unchanged");
    currentJob=112;
    missingResource=true;
    void* entry=reinterpret_cast<void*>(7);
    const int previousLogs=logCount;
    Require(casting(database,nullptr,character,1066,&entry) == 0 && !entry, "missing resource fails closed");
    Require(query(database,nullptr,character,1067,nullptr) == 0 && logCount == previousLogs+1, "missing resource logs once");
    missingResource=false;
    Require(ChooseAction(9,44,nullptr,2,0x1234) == 122 && actionCount == 1, "native action fallback");
}

void GateTests() {
    int storage[128]{};
    int* frame=storage+64;
    currentJob=2218;
    for (int morph=64; morph<=67; ++morph) {
        Set(user,0x528,morph);
        for (int baseSkill : {1066,1067}) {
            int* entry=&entries[baseSkill==1066 ? 6 : 7];
            auto registers=State(frame);
            registers.ecx=reinterpret_cast<DWORD>(user+0x88);
            registers.esi=*entry;
            Invoke(Target(0x0096754D),&registers);
            Require(registers.eax == 0 && registers.ecx == reinterpret_cast<DWORD>(user+0x88)
                && registers.esi == static_cast<DWORD>(*entry), "morph gate narrow exception ABI");
            frame[-4]=reinterpret_cast<int>(entry);
            frame[-5]=morph-63;
            registers=State(frame);
            registers.ebx=reinterpret_cast<DWORD>(user);
            registers.esi=*entry;
            Invoke(Target(0x009678F9),&registers);
            Require(registers.eax == 81 && lastLevel == morph-63 && lastSkill == *entry,
                "dispatch executes native 9690AE entry and level pushes");
            frame[4]=reinterpret_cast<int>(entry);
            frame[5]=morph-63;
            registers=State(frame);
            registers.esi=reinterpret_cast<DWORD>(user);
            registers.edi=baseSkill==1066 ? 16 : 17;
            Invoke(Target(0x0092EE27),&registers);
            Require(registers.eax == 101, "action permission accepted");
            int damage=-1;
            frame[-18]=reinterpret_cast<int>(&damage);
            frame[2]=reinterpret_cast<int>(character);
            frame[14]=reinterpret_cast<int>(entry);
            frame[15]=morph-63;
            Set(levelData,0xDC,12345*(morph-63));
            registers=State(frame);
            registers.ebx=reinterpret_cast<DWORD>(levelData);
            Invoke(Target(0x0078E6BC),&registers);
            Require(damage == 12345*(morph-63), "fixed damage reaches native damage slot");
            Require(registers.ebx == reinterpret_cast<DWORD>(levelData) && registers.esi == 0x5555,
                "damage hook preserves live registers");
        }
    }
    Require(meleeCount == 8, "both families all morph levels dispatched once");
    for (int skill : {1066,20011068,22181001}) {
        Set(user,0x528,64);
        auto registers=State(frame);
        registers.ecx=reinterpret_cast<DWORD>(user+0x88);
        registers.esi=skill;
        Invoke(Target(0x0096754D),&registers);
        Require(registers.eax == 19, "unrelated morph gate returns original predicate");
        registers=State(frame);
        registers.ebx=reinterpret_cast<DWORD>(user);
        registers.esi=skill;
        Invoke(Target(0x009678F9),&registers);
        Require(registers.eax == 0x1111 && registers.esi == static_cast<DWORD>(skill), "dispatch fallback preserves registers");
    }
    Set(user,0x528,63);
    frame[4]=reinterpret_cast<int>(&entries[6]);
    frame[5]=1;
    auto registers=State(frame);
    registers.esi=reinterpret_cast<DWORD>(user);
    registers.edi=16;
    Invoke(Target(0x0092EE27),&registers);
    Require(registers.ecx == reinterpret_cast<DWORD>(&entries[6]) && !(registers.flags&0x40), "action fallback replays MOV and TEST");
    int unchanged=0x123456;
    frame[-18]=reinterpret_cast<int>(&unchanged);
    frame[2]=reinterpret_cast<int>(character);
    frame[14]=reinterpret_cast<int>(&entries[6]);
    frame[15]=1;
    registers=State(frame);
    registers.ebx=reinterpret_cast<DWORD>(levelData);
    Invoke(Target(0x0078E6BC),&registers);
    Require(registers.eax == 103 && unchanged == 0x123456, "damage fallback replays checksum PUSH");
    Set(user,0x528,64);
    Require(!AcceptAction(user,&entries[6],1,17) && !AcceptAction(user,&entries[6],0,16), "wrong action or level denied");
}

void NativeConditionsAndReactor() {
    const unsigned char matchBytes[] = {0x8b,0x75,0x08,0x39,0x34,0xb9,0x75,0x40};
    const unsigned char skillWireBytes[] = {0xff,0x75,0x08,0x8d,0x4d,0xc0,0xe8,0xf1,0x08,0xcd,0xff};
    Require(std::memcmp(reinterpret_cast<void*>(Native(0x00735BCA)),matchBytes,sizeof(matchBytes)) == 0,
        "native reactor compares full activeSkillID");
    Require(std::memcmp(reinterpret_cast<void*>(Native(0x00735CAA)),skillWireBytes,sizeof(skillWireBytes)) == 0,
        "native reactor encodes unmodified skill argument");
    *reinterpret_cast<DWORD*>(Native(0x007642A9))=Native(0x00BE78DC);
    *reinterpret_cast<void**>(Native(0x00BE78DC))=database;
    Jump(Native(0x007642BE),reinterpret_cast<void*>(&ConditionsReturn));
    Jump(Native(0x00735BD2),reinterpret_cast<void*>(&Accepted));
    Jump(Native(0x00735C12),reinterpret_cast<void*>(&Rejected));
    Jump(Native(0x006EC9CE),reinterpret_cast<void*>(&PacketStart));
    Jump(Native(0x004065A6),reinterpret_cast<void*>(&EncodeFour));
    Jump(Native(0x00427F74),reinterpret_cast<void*>(&EncodeTwo));
    *reinterpret_cast<unsigned char*>(Native(0x00735CB5))=0xC3;
    using Conditions = int(__thiscall*)(void*, void*, void*, void*, int);
    const auto conditions=reinterpret_cast<Conditions>(Native(0x00764256));
    int storage[128]{}, reactor[16]{};
    int* frame=storage+64;
    reactor[0]=0x123456;
    reactor[13]=3;
    Set(user,0x528,67);
    const int jobs[] = {112,1112,2112,2218};
    for (int index=0; index<8; ++index) {
        currentJob=jobs[index/2];
        const int skill=entries[index];
        Require(conditions(database,character,nullptr,nullptr,skill) == 1,
            "native 764256 reaches synthesized ordinary lookup without learned skill");
        frame[2]=skill;
        int acceptedSkill=entries[index & ~1];
        auto registers=State(frame);
        registers.ecx=reinterpret_cast<DWORD>(&acceptedSkill);
        registers.edi=0;
        Invoke(reinterpret_cast<void*>(Native(0x00735BCA)),&registers);
        Require(registers.eax == (index%2 ? 102u : 101u), "reactor whitelist accepts melee prefix, rejects range prefix");
        frame[-9]=reinterpret_cast<int>(reactor);
        registers=State(frame);
        registers.edi=800;
        Invoke(reinterpret_cast<void*>(Native(0x00735C6B)),&registers);
        Require(reactorPacket.size() == 20 && reactorPacket[0] == 0xCD && reactorPacket[1] == 0,
            "native HIT_REACTOR packet size and opcode");
        int encodedSkill=0;
        std::memcpy(&encodedSkill,reactorPacket.data()+16,4);
        Require(encodedSkill == skill,"native HIT_REACTOR preserves full prefixed skill ID at offset 16");
    }
}
}

void ClientLog::Append(Component, const char* format, ...) {
    ++logCount;
    if (std::strstr(format,"reason=signature")) ++signatureLogs;
}

int main(int argc, char** argv) {
    Require(argc == 2,"EXE path required");
    void* image=LoadImage(argv[1]);
    InstallTests(image);
    methods[16]=reinterpret_cast<void*>(&Job);
    Set(user,0,methods);
    Set(otherUser,0,methods);
    Set(context,0x20B8,static_cast<void*>(character));
    Set(levelData,0xE4,0x89ABCDEFu);
    *reinterpret_cast<void**>(Native(0x00BEBF98))=user;
    *reinterpret_cast<void**>(Native(0x00BE7918))=context;
    Jump(Native(0x0075C755),reinterpret_cast<void*>(&Resource));
    Jump(Native(0x00966F7A),reinterpret_cast<void*>(&Cast));
    Jump(Native(0x007616FD),reinterpret_cast<void*>(&LevelFallback));
    Jump(Native(0x00766031),reinterpret_cast<void*>(&NativeAction));
    Jump(Native(0x00416563),reinterpret_cast<void*>(&SafeInt));
    Jump(Native(0x004520A4),reinterpret_cast<void*>(&NativeMorph));
    Jump(Native(0x00969465),reinterpret_cast<void*>(&Melee));
    Jump(Native(0x0094E45A),reinterpret_cast<void*>(&Accepted));
    Jump(Native(0x0092EE96),reinterpret_cast<void*>(&Accepted));
    Jump(Native(0x0079059E),reinterpret_cast<void*>(&StoreDamage));
    Jump(Native(0x0078E6C2),reinterpret_cast<void*>(&DamageFallback));
    for (DWORD address : {0x0094E2B9u,0x009678FFu,0x00967B7Eu,0x0092EE2Cu})
        *reinterpret_cast<unsigned char*>(Native(address))=0xC3;
    LevelsAndInputs();
    GateTests();
    NativeConditionsAndReactor();
    std::puts("PASS: 9 disk signatures, fail-closed install, x86 gate ABI, prefixes, levels, input, actions, damage and non-Visitors fallbacks");
    std::puts("PASS: native 764256 query path and native HIT_REACTOR whitelist/encoding blocks (mocked encoders; no send)");
    std::puts("No game client, server or network session was started.");
    return 0;
}
