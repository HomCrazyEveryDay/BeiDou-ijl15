#include "../ezorsia/VisitorsScroll.cpp"
#include "../ezorsia/MixedDyeResources.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#pragma comment(lib,"ole32.lib")

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

int signatureLogs = 0, loggedMatches = 0, loggedUses = 0, resourceCalls = 0, resourceMode = 0;
std::map<std::wstring, int> categories;
void* resourceMethods[8]{};
void** resourceManager = resourceMethods;
std::vector<int> materials = {2046215,2046216,2046217,2046218,2046400,2046401,2046402,2046403,
    2471000,2490000,2490001,2490002,2490003};
std::vector<int> equipmentIds;

HRESULT __stdcall GetObject(void* manager, BSTR path, VARIANT first, VARIANT second, VARIANT* value) {
    Require(manager == &resourceManager, "resource manager receiver");
    Require(first.vt == VT_ERROR && second.vt == VT_ERROR
        && first.scode == DISP_E_PARAMNOTFOUND && second.scode == DISP_E_PARAMNOTFOUND, "resource optional arguments");
    ++resourceCalls;
    if (resourceMode == 1) return E_FAIL;
    if (resourceMode == 2) return S_OK;
    if (resourceMode == 3) { value->vt = VT_BSTR; value->bstrVal = SysAllocString(L"2"); return S_OK; }
    if (resourceMode == 4) RaiseException(0xE1234567, 0, 0, nullptr);
    auto found = categories.find(path);
    Require(found != categories.end(), "exact whitelisted resource path");
    value->vt = resourceMode == 5 ? VT_I2 : VT_I4;
    value->lVal = found->second;
    return S_OK;
}

void InstallTests(void* image) {
    Require(!VisitorsScroll::Install(nullptr), "wrong module rejected");
    std::vector<Signature> signatures(std::begin(dependencies), std::end(dependencies));
    for (const auto& patch : patches) signatures.push_back(patch.signature);
    for (const auto& signature : signatures) {
        auto* site = reinterpret_cast<unsigned char*>(signature.address);
        Require(std::memcmp(site, signature.expected, signature.size) == 0, "disk signature");
        for (size_t offset = 0; offset < signature.size; ++offset) {
            site[offset] ^= 1;
            const int previous = signatureLogs;
            Require(!VisitorsScroll::Install(static_cast<HMODULE>(image)), "each signature byte fails closed");
            Require(signatureLogs == previous + 1, "signature mismatch diagnostic");
            site[offset] ^= 1;
            for (const auto& patch : patches)
                Require(std::memcmp(reinterpret_cast<void*>(patch.signature.address), patch.signature.expected,
                    patch.signature.size) == 0, "no partial install");
        }
    }
    auto* header = reinterpret_cast<IMAGE_NT_HEADERS32*>(static_cast<unsigned char*>(image)
        + static_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
    header->FileHeader.TimeDateStamp ^= 1;
    Require(!VisitorsScroll::Install(static_cast<HMODULE>(image)), "wrong PE timestamp rejected");
    header->FileHeader.TimeDateStamp ^= 1;
    Set(image, Native(0xAF34DC) - Native(0x400000), 0x12345678u);
    const DWORD otherHooks[] = {Native(0x004EFF30),Native(0x004EFEAD),Native(0x0094E2B3),Native(0x0094FA28),Native(0x007616F6)};
    for (DWORD address : otherHooks) *reinterpret_cast<BYTE*>(address) = 0xE9;
    unsigned char nativePredicate[174], nativeSender[218];
    std::memcpy(nativePredicate, reinterpret_cast<void*>(Native(0x4F5497)), sizeof(nativePredicate));
    std::memcpy(nativeSender, reinterpret_cast<void*>(Native(0xA09221)), sizeof(nativeSender));
    Require(VisitorsScroll::Install(static_cast<HMODULE>(image)), "install beside existing hooks");
    Require(VisitorsScroll::Install(static_cast<HMODULE>(image)), "idempotent install");
    for (const auto& patch : patches) Require(Target(patch.signature.address) == patch.target, "patch destination");
    Require(*reinterpret_cast<DWORD*>(Native(0xAF34DC)) == 0x12345678, "MineralBag drag vtable retained");
    for (DWORD address : otherHooks) Require(*reinterpret_cast<BYTE*>(address) == 0xE9, "other hook retained");
    Require(std::memcmp(nativePredicate, reinterpret_cast<void*>(Native(0x4F5497)), sizeof(nativePredicate)) == 0,
        "native global match including other UI unchanged");
    Require(std::memcmp(nativeSender, reinterpret_cast<void*>(Native(0xA09221)), sizeof(nativeSender)) == 0,
        "entire native sender unchanged");
}

void MatchingTests() {
    resourceMethods[7] = reinterpret_cast<void*>(&GetObject);
    *reinterpret_cast<void**>(Native(0xBF14E8)) = &resourceManager;
    for (int material : materials) {
        wchar_t path[112];
        swprintf_s(path, L"Item/Consume/%04d.img/%08d/info/enchantCategory", material / 10000, material);
        categories[path] = 2;
    }
    const int firstIds[] = {1003115,1052277,1072449,1082277};
    const wchar_t* folders[] = {L"Cap",L"Longcoat",L"Shoes",L"Glove"};
    for (int group = 0; group < 4; ++group) {
        for (int variant = 0; variant < 5; ++variant) {
            const int equipment = firstIds[group] + variant;
            equipmentIds.push_back(equipment);
            wchar_t path[112];
            swprintf_s(path, L"Character/%s/%08d.img/info/enchantCategory", folders[group], equipment);
            categories[path] = 2;
        }
    }
    const auto match = reinterpret_cast<NativeMatch>(Target(Native(0x4F4E1C)));
    for (int material : materials) for (int equipment : equipmentIds) {
        SetLastError(31415);
        Require(match(material, equipment) == 1, "all 13 x 20 Visitors pairs accepted");
        Require(GetLastError() == 31415, "last error retained");
    }
    for (int category : {-1,0,1,3,6}) {
        categories[L"Item/Consume/0247.img/02471000/info/enchantCategory"] = category;
        Require(match(2471000,1003115) == 0, "source category must be exactly 2");
        categories[L"Item/Consume/0247.img/02471000/info/enchantCategory"] = 2;
        categories[L"Character/Cap/01003115.img/info/enchantCategory"] = category;
        Require(match(2471000,1003115) == 0, "target category must be exactly 2");
        categories[L"Character/Cap/01003115.img/info/enchantCategory"] = 2;
    }
    for (resourceMode = 1; resourceMode <= 4; ++resourceMode)
        Require(match(2471000,1003115) == 0, "missing, bad type, failed HRESULT and resource exception reject");
    resourceMode = 5;
    Require(match(2471000,1003115) == 1, "WZ short integer accepted");
    resourceMode = 0;
    *reinterpret_cast<void**>(Native(0xBF14E8)) = nullptr;
    Require(match(2471000,1003115) == 0, "uninitialized resource manager rejects");
    *reinterpret_cast<void**>(Native(0xBF14E8)) = &resourceManager;
    for (int equipment : {0,-1,1002000,1003114,1003120,1052276,1052282,1072448,1072454,1082276,1082282,
            1092000,1620000,1640000,2000000}) {
        const int previous = resourceCalls;
        for (int material : materials) Require(match(material,equipment) == 0, "other targets rejected");
        Require(resourceCalls == previous, "non-whitelist targets do not load resources");
    }
    const int previous = resourceCalls;
    const int nativeTargets[] = {1002000,1003115,1052277,1072449,1082277,1112000,1122000,1132000,1302000,1802000,2000000};
    for (int material = 2040000; material < 2050000; ++material) {
        if (IsMaterial(material)) continue;
        for (int equipment : nativeTargets)
            Require(match(material,equipment) == nativeMatch(material,equipment), "ordinary scrolls execute original predicate");
    }
    for (int material : {2000000,2046214,2046219,2046399,2046404,2470000,2470999,2471001,2480000,2489999,2490004,2491000})
        for (int equipment : equipmentIds)
            Require(match(material,equipment) == nativeMatch(material,equipment), "adjacent IDs stay native");
    Require(resourceCalls == previous && loggedMatches == 32, "no ordinary-resource work; match logs bounded");
}

unsigned char context[0x20B0]{}, character[32]{}, sourceItem[64]{}, targetItem[64]{}, draggable[48]{};
unsigned char legendary[0x600]{}, checkbox[0x50]{}, localUser[32]{};
void* itemMethods[2]{}, *windowMethods[19]{};
bool inputAllowed = true, throttleAllowed = true, haveTarget = true;
int targetSlot = -1, whiteCount = 0, dialogResult = 6, dialogId = 0, dialogCount = 0, resetCount = 0;
int fallbackUses = 0, nativeUseResult = 0, sentCount = 0;
std::vector<unsigned char> packet, sentPacket;
int __fastcall InputAllowed(void*, void*, int) { return inputAllowed; }
int __fastcall Throttle(void* instance, void*, int interval, int zero) {
    Require(instance == context && interval == 500 && zero == 0, "native input throttle arguments");
    return throttleAllowed;
}
DWORD* __fastcall Character(void* instance, void*, DWORD* reference) {
    Require(instance == context, "character context");
    reference[0] = 0; reference[1] = reinterpret_cast<DWORD>(character); return reference;
}
DWORD* __fastcall Item(void* instance, void*, DWORD* reference, int inventory, int slot) {
    Require(instance == character, "inventory character");
    if (inventory == 2) Require(slot == *reinterpret_cast<int*>(draggable + 0x1C), "material slot");
    else Require(inventory == 1 && slot == targetSlot, "native signed target slot");
    reference[0] = 0;
    reference[1] = reinterpret_cast<DWORD>(inventory == 2 ? sourceItem : haveTarget ? targetItem : nullptr);
    return reference;
}
void __fastcall ReleaseRef(void*, void*) {}
void __fastcall ReleaseRefWithFlag(void*, void*, int) {}
int __fastcall ItemId(void* instance, void*) { return *static_cast<int*>(instance); }
int __fastcall Cash(void* instance, void*) { return *reinterpret_cast<int*>(static_cast<unsigned char*>(instance) + 16); }
int __fastcall EquipSlot(void*, void*, int horizontal, int vertical) {
    Require(horizontal == 17 && vertical == 23, "native drop coordinates retained");
    return -targetSlot;
}
int __fastcall IsLegendary(void* instance, void*, void* type) {
    Require(instance == legendary + 4 && type == reinterpret_cast<void*>(Native(0xBF0E18)), "native legendary UI identity");
    return 1;
}
int __fastcall WhiteScroll(void* instance, void*, int inventory, int itemId) {
    Require(instance == character && inventory == 2 && itemId == 2340000, "native white scroll query");
    return whiteCount;
}
void* __cdecl StringPool() { return context; }
void* __fastcall DialogString(void*, void*, DWORD* result, int key) { dialogId = key; *result = 0; return result; }
int __cdecl Dialog(void*, int, int, int, int) { ++dialogCount; return dialogResult; }
void* __fastcall PacketStart(void* instance, void*, unsigned opcode) {
    packet = {static_cast<unsigned char>(opcode), static_cast<unsigned char>(opcode >> 8)};
    return instance;
}
void Encode(unsigned value, int width) {
    for (int index = 0; index < width; ++index) packet.push_back(static_cast<unsigned char>(value >> (index * 8)));
}
void __fastcall EncodeFour(void*, void*, unsigned value) { Encode(value,4); }
void __fastcall EncodeTwo(void*, void*, unsigned value) { Encode(value,2); }
void __fastcall EncodeOne(void*, void*, unsigned value) { Encode(value,1); }
unsigned __cdecl Tick() { return 0x12345678; }
void __fastcall Send(void* instance, void*, void*) {
    Require(instance == reinterpret_cast<void*>(0x33445566), "native socket receiver (mock, never network)");
    sentPacket = packet; ++sentCount;
}
void __fastcall ResetLegendary(void* instance, void*) { Require(instance == legendary, "native UI reset receiver"); ++resetCount; }
int __fastcall NativeUse(void* instance, void*) { Require(instance == localUser, "fallback use receiver"); ++fallbackUses; return nativeUseResult; }

void NativeMocks() {
    const DWORD globals[] = {0xBE7910,0xBE7914,0xBE7918,0xBED648,0xBED64C,0xBF0E18};
    for (const auto& block : {std::make_pair(0x4F4BB7u,2272u),std::make_pair(0xA09221u,218u)}) {
        auto* code = reinterpret_cast<unsigned char*>(Native(block.first));
        for (size_t offset = 0; offset + sizeof(DWORD) <= block.second; ++offset) {
            auto* operand = reinterpret_cast<DWORD*>(code + offset);
            for (DWORD address : globals) if (*operand == address) { *operand = Native(address); break; }
        }
    }
    itemMethods[1] = reinterpret_cast<void*>(&Cash);
    Set(sourceItem,0,itemMethods); Set(targetItem,0,itemMethods);
    Set(draggable,0x18,2); Set(draggable,0x1C,37);
    windowMethods[18] = reinterpret_cast<void*>(&IsLegendary);
    Set(legendary,4,windowMethods); Set(legendary,0x5D0,checkbox);
    *reinterpret_cast<void**>(Native(0xBE7918)) = context;
    *reinterpret_cast<void**>(Native(0xBE7914)) = reinterpret_cast<void*>(0x33445566);
    *reinterpret_cast<void**>(Native(0xBE7910)) = legendary;
    Jump(Native(0x4F5697),InputAllowed); Jump(Native(0x485BF7),Throttle); Jump(Native(0x425D0B),Character);
    Jump(Native(0x4282F7),Item); Jump(Native(0x4288C1),ReleaseRef); Jump(Native(0x4289B7),ReleaseRef);
    Jump(Native(0x428C44),ReleaseRefWithFlag); Jump(Native(0x428A50),ReleaseRefWithFlag); Jump(Native(0x42873D),ItemId);
    Jump(Native(0x7FEC32),EquipSlot); Jump(Native(0x4E6FD2),WhiteScroll); Jump(Native(0x79E805),StringPool);
    Jump(Native(0x406455),DialogString); Jump(Native(0x992BFD),Dialog); Jump(Native(0x6EC9CE),PacketStart);
    Jump(Native(0x4065A6),EncodeFour); Jump(Native(0x427F74),EncodeTwo); Jump(Native(0x406549),EncodeOne);
    Jump(Native(0x987257),Tick); Jump(Native(0x49637B),Send); Jump(Native(0x428CF1),ReleaseRef);
    Jump(Native(0x7F64EC),ResetLegendary); Jump(Native(0x4F0D45),NativeUse);
}

void CheckPacket(int expectedSlot, int flags, int extra) {
    const int sourceSlot = *reinterpret_cast<int*>(draggable + 0x1C);
    std::vector<unsigned char> expected = {0x56,0,0x78,0x56,0x34,0x12,
        static_cast<unsigned char>(sourceSlot),static_cast<unsigned char>(sourceSlot >> 8),
        static_cast<unsigned char>(expectedSlot),static_cast<unsigned char>(expectedSlot >> 8),
        static_cast<unsigned char>(flags),0,static_cast<unsigned char>(extra)};
    Require(sentPacket == expected && sentPacket.size() == 13, "exact native 0x56: 2-byte opcode plus 11-byte body");
    Require(*reinterpret_cast<int*>(context + 0x20A4) == 1
        && *reinterpret_cast<unsigned*>(context + 0x20A8) == 0x12345678, "native pending/tick bookkeeping");
}

using Drop = int(__thiscall*)(void*,int,int,int,int);
const auto drop = reinterpret_cast<Drop>(Native(0x4F4BB7));
void NativeFlowTests() {
    for (int material : materials) for (int equipment : equipmentIds) {
        Set(sourceItem,12,material); Set(targetItem,12,equipment);
        for (int slot : {-1,-5,-7,-8,1,17,127}) {
            targetSlot = slot; Set(legendary,0x94,slot);
            const int previous = sentCount;
            Require(drop(draggable,17,23,slot > 0,0) == 1, "native drop succeeds");
            Require(sentCount == previous + 1, "exactly one native send");
            CheckPacket(slot,1,slot > 0);
        }
    }
    Set(sourceItem,12,2471000); Set(targetItem,12,1003115); targetSlot = -1;
    whiteCount = 1;
    for (int answer : {6,7}) {
        dialogResult = answer;
        const int previous = dialogCount;
        Require(drop(draggable,17,23,0,0) == 1, "native white-scroll choice proceeds");
        Require(dialogCount == previous + 1 && dialogId == 0xF7B, "native white-scroll dialog unchanged");
        CheckPacket(-1,answer == 6 ? 2 : 1,0);
    }
    targetSlot = 17; Set(legendary,0x94,17);
    for (int selected : {0,1}) {
        Set(checkbox,0x48,selected);
        const int previous = dialogCount;
        Require(drop(draggable,17,23,1,0) == 1, "legendary scroll path");
        Require(dialogCount == previous, "legendary white checkbox uses native no-dialog branch");
        CheckPacket(17,selected + 1,1);
    }
    whiteCount = 0; targetSlot = -1; Set(targetItem,16,1); dialogResult = 7;
    int previous = sentCount;
    Require(drop(draggable,17,23,0,0) == 0 && sentCount == previous, "native cash-equipment confirmation cancellation");
    Require(dialogId == 0x114F, "native cash confirmation text");
    dialogResult = 6;
    Require(drop(draggable,17,23,0,0) == 1, "native cash-equipment confirmation acceptance");
    CheckPacket(-1,1,0); Set(targetItem,16,0);
    previous = sentCount;
    inputAllowed = false;
    Require(drop(draggable,17,23,0,0) == 0 && sentCount == previous, "native input state still blocks");
    inputAllowed = true; throttleAllowed = false;
    Require(drop(draggable,17,23,0,0) == 0 && sentCount == previous, "native throttle still blocks");
    throttleAllowed = true; haveTarget = false;
    Require(drop(draggable,17,23,0,0) == 0 && sentCount == previous, "empty slot rejected");
    haveTarget = true;
    Set(legendary,0x5AC,1);
    Require(drop(draggable,17,23,1,0) == 0 && sentCount == previous, "busy legendary UI rejected");
    Set(legendary,0x5AC,0);
    Set(sourceItem,12,2040000); Set(targetItem,12,1002000);
    Require(drop(draggable,17,23,0,0) == 1, "ordinary hat scroll still sends natively");
    CheckPacket(-1,1,0);
}

__declspec(naked) int Blocked() { __asm { mov eax, 111 } __asm { ret } }
__declspec(naked) int ContinueUse() { __asm { mov eax, 222 } __asm { ret } }
const DWORD useEntry = Native(0x4F0307);
__declspec(naked) int InvokeUse(void*,void*) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 20h
        push esi
        mov esi, [ebp+8]
        mov eax, [ebp+0ch]
        mov [ebp-20h], eax
        mov eax, dword ptr [useEntry]
        call eax
        pop esi
        leave
        ret
    }
}
void UseTests() {
    Jump(Native(0x4EFD4C),Blocked); Jump(Native(0x4F0317),ContinueUse);
    for (int itemId : {2471000,2490000,2490001,2490002,2490003}) {
        Set(sourceItem,12,itemId);
        const int previous = fallbackUses;
        Require(InvokeUse(sourceItem,localUser) == 111, "247/249 ends at original no-use exit");
        Require(fallbackUses == previous, "blocked material never reaches use predicate or sender");
    }
    for (int itemId : {2000000,2040000,2046215,2046403,2470000,2471001,2490004}) {
        Set(sourceItem,12,itemId);
        for (int blocked : {0,1}) {
            nativeUseResult = blocked;
            const int previous = fallbackUses;
            Require(InvokeUse(sourceItem,localUser) == (blocked ? 111 : 222), "ordinary use fallback result");
            Require(fallbackUses == previous + 1, "ordinary use predicate called once");
        }
    }
    for (int attempt = 0; attempt < 64; ++attempt) BlockUse(2471000);
    Require(loggedUses == 8, "double-click diagnostics bounded");
}

void InstalledResourceTests(const char* clientRoot) {
    using namespace MixedDyeResources;
    Require(SetCurrentDirectoryA(clientRoot) != FALSE, "installed client resource directory");
    Require(SUCCEEDED(CoInitialize(nullptr)), "COM test initialization");
    auto module = LoadLibraryW(L"PCOM.dll");
    Require(module != nullptr, "PCOM without launching client");
    MixedDyeResources::Check(reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(module,"PcInitModule"))());
    std::atexit([] { reinterpret_cast<void(__cdecl*)()>(GetProcAddress(GetModuleHandleW(L"PCOM.dll"),"PcTermModule"))(); });
    auto factory = reinterpret_cast<Factory>(GetProcAddress(module,"PcCreateObject"));
    const GUID managerId = {0x57dfe40b,0x3e20,0x4dbc,{0x97,0xe8,0x80,0x5a,0x50,0xf3,0x81,0xbf}};
    const GUID namespaceId = {0x2aeeeb36,0xa4e1,0x4e2b,{0x8f,0x6f,0x2e,0x7b,0xde,0xc5,0xc5,0x3d}};
    const GUID filesystemId = {0x352d8655,0x51e4,0x4668,{0x8c,0xe4,0x08,0x66,0xe2,0xb6,0xa5,0xb5}};
    auto manager = New(factory,L"ResMan",managerId);
    auto root = New(factory,L"NameSpace",namespaceId);
    auto filesystem = New(factory,L"NameSpace#FileSystem",filesystemId);
    MixedDyeResources::Check(Method<HRESULT(__stdcall*)(void*,int,int,int)>(manager.p,0x14)(manager.p,0x11,-1,-1));
    using Root = HRESULT(__cdecl*)(void**,int);
    MixedDyeResources::Check(reinterpret_cast<Root>(GetProcAddress(module,"PcRootNameSpace"))(&root.p,1));
    BSTR path = SysAllocString(L"Data");
    MixedDyeResources::Check(Method<HRESULT(__stdcall*)(void*,BSTR)>(filesystem.p,0x34)(filesystem.p,path));
    SysFreeString(path);
    path = SysAllocString(L"/");
    MixedDyeResources::Check(Method<HRESULT(__stdcall*)(void*,BSTR,void*,int)>(root.p,0x18)(root.p,path,filesystem.p,0));
    SysFreeString(path);
    *reinterpret_cast<void**>(Native(0xBF14E8)) = manager.p;
    for (int itemId : materials) {
        const int category = Category(itemId);
        if (category != 2) std::fprintf(stderr,"material=%d category=%d\n",itemId,category);
        Require(category == 2, "installed material enchantCategory");
        for (const wchar_t* icon : {L"icon",L"iconRaw"}) {
            wchar_t resource[112];
            swprintf_s(resource,L"Item/Consume/%04d.img/%08d/info/%s",itemId / 10000,itemId,icon);
            path = SysAllocString(resource);
            Value value;
            const HRESULT result = ResourceValue(path,&value.v);
            SysFreeString(path);
            MixedDyeResources::Check(result);
            auto canvas = Query(value.v,CanvasIID());
            Require(canvas.p && Int(canvas.p,0x40) > 0 && Int(canvas.p,0x48) > 0,"installed material native icon canvas");
        }
    }
    for (int itemId : equipmentIds) {
        const int category = Category(itemId);
        if (category != 2) std::fprintf(stderr,"equipment=%d category=%d\n",itemId,category);
        Require(category == 2,"installed equipment enchantCategory");
    }
    *reinterpret_cast<void**>(Native(0xBF14E8)) = nullptr;
    std::puts("PASS: real PCOM reads installed 13 material + 20 equipment categories and 26 icon/iconRaw canvases");
}
}

void ClientLog::Append(Component, const char* format, ...) {
    if (std::strstr(format,"reason=signature")) ++signatureLogs;
    if (std::strstr(format,"event=visitors_scroll_match")) ++loggedMatches;
    if (std::strstr(format,"event=visitors_scroll_double_click")) ++loggedUses;
}

int main(int argc, char** argv) {
    Require(argc == 2 || argc == 3, "EXE path and optional installed resource directory");
    void* image = LoadImage(argv[1]);
    InstallTests(image);
    MatchingTests();
    NativeMocks();
    NativeFlowTests();
    UseTests();
    if (argc == 3) InstalledResourceTests(argv[2]);
    std::puts("PASS: 2 patch + 5 dependency signatures, per-byte fail-closed install and hook coexistence");
    std::puts("PASS: 260 pairs, actual resource paths/categories, missing resources, ordinary scroll equivalence");
    std::puts("PASS: native 4F4BB7 -> A09221 executed: signed slots, exact 0x56, white/cash confirmations and pending state");
    std::puts("PASS: native double-click branch ABI and bounded diagnostics; no client/server/network launched");
    return 0;
}
