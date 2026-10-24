#include "../ezorsia/SetItemBonus.cpp"
#include "VisitorsShuttleTest.h"
#include <thread>
using namespace TestSupport;

namespace SetItemTooltip {
int refreshCalls = 0;
void Refresh() { ++refreshCalls; }
}

namespace {
unsigned char context[0x4000]{}, otherSecondary[0x200]{}, character[16]{}, otherCharacter[16]{};
int refreshCount = 0, readCount = 0, writeCount = 0, logCount = 0;
DWORD testThread = 0;
bool nestedRefresh = false;
unsigned char ordinary[6] = {0,0,0,0,0x7E,0};
const int baseValues[] = {100,200,300,400};

unsigned __fastcall WriteInt(int value, void* encoded) {
    ++writeCount;
    Set(encoded, 0, static_cast<unsigned>(value) ^ 0x13579BDFu);
    Set(encoded, 4, 0xABCDEF01u);
    return static_cast<unsigned>(value) ^ 0x2468ACE0u;
}
int __cdecl ReadInt(const void* encoded, unsigned checksum) {
    ++readCount;
    const auto* fields = static_cast<const unsigned*>(encoded);
    const unsigned value = fields[0] ^ 0x13579BDFu;
    Require(fields[1] == 0xABCDEF01 && checksum == (value ^ 0x2468ACE0u), "secure int checksum and reader cdecl ABI");
    return static_cast<int>(value);
}
void InitializeStats(void* secondary) {
    for (size_t index = 0; index < 4; ++index) {
        auto* encoded = static_cast<unsigned char*>(secondary) + offsets[index];
        Set(encoded, 8, WriteInt(baseValues[index], encoded));
        Set(encoded, 12, 0x12345678u);
    }
}
void ExpectValues(const int* bonuses) {
    for (size_t index = 0; index < 4; ++index) {
        auto* encoded = context + 0x2134 + offsets[index];
        Require(ReadInt(encoded, *reinterpret_cast<unsigned*>(encoded + 8)) == baseValues[index] + bonuses[index], "rebuilt totals include one snapshot");
        Require(*reinterpret_cast<unsigned*>(encoded + 12) == 0x12345678, "temporary buff fields untouched");
    }
}
void __fastcall Validate(void* receiver, void*) {
    Require(receiver == context && GetCurrentThreadId() == testThread, "ValidateStat receiver and game-thread affinity");
    ++refreshCount;
    InitializeStats(context + 0x2134);
    ApplyBonus(context + 0x2134);
    if (nestedRefresh) {
        Require(!SetItemBonus::HandlePacket(ordinary, sizeof(ordinary)), "nested non-owned packet passes through");
    }
}
std::vector<unsigned char> Packet(unsigned pieces, int pdd, int mdd, int acc, int eva) {
    std::vector<unsigned char> packet(16);
    packet[4] = 0x11; packet[5] = 0x10; packet[6] = 1; packet[7] = static_cast<unsigned char>(pieces);
    const int values[] = {pdd,mdd,acc,eva};
    for (size_t index = 0; index < 4; ++index) {
        packet[8 + index * 2] = static_cast<unsigned char>(values[index]);
        packet[9 + index * 2] = static_cast<unsigned char>(values[index] >> 8);
    }
    return packet;
}
bool Receive(const std::vector<unsigned char>& packet) {
    return SetItemBonus::HandlePacket(packet.data(), static_cast<unsigned long>(packet.size()));
}
void InstallTests(HMODULE image) {
    Require(!SetItemBonus::Install(nullptr), "wrong base fails closed");
    for (const auto& signature : signatures) {
        auto* site = reinterpret_cast<unsigned char*>(signature.address);
        Require(!std::memcmp(site, signature.expected, signature.size), "on-disk patch and secure/Validate ABI signatures");
        site[0] ^= 1;
        Require(!SetItemBonus::Install(image), "signature mismatch rejected");
        site[0] ^= 1;
        Require(!std::memcmp(reinterpret_cast<void*>(signatures[0].address), signatures[0].expected, signatures[0].size), "failed install leaves site unchanged");
    }
    Require(logCount == 1, "installation failure logged once");
    auto* site = reinterpret_cast<unsigned char*>(signatures[0].address);
    DWORD ignored;
    VirtualProtect(site, 6, PAGE_EXECUTE_READ, &ignored);
    Require(SetItemBonus::Install(image) && SetItemBonus::Install(image), "successful idempotent installation");
    Require(site[0] == 0xE9 && site[5] == 0x90 && site + 5 + *reinterpret_cast<int*>(site + 1) == reinterpret_cast<void*>(BeforeEquipment), "full pre-equipment JMP");
    MEMORY_BASIC_INFORMATION information{};
    VirtualQuery(site, &information, sizeof(information));
    Require(information.Protect == PAGE_EXECUTE_READ, "code page restored to RX");
    VirtualProtect(site, 6, PAGE_EXECUTE_READWRITE, &ignored);
}

void PacketTests() {
    Require(!SetItemBonus::HandlePacket(nullptr, 16), "null packet");
    auto packet = Packet(4, 20, 20, 20, 20);
    Require(Receive(packet) && refreshCount == 1, "one refresh per snapshot");
    const int full[] = {20,20,20,20}, partial[] = {20,20,0,0}, zeros[] = {0,0,0,0};
    ExpectValues(full);
    Require(Receive(packet) && refreshCount == 2, "duplicate snapshot rebuilds instead of adding directly");
    ExpectValues(full);
    for (int repeat = 0; repeat < 3; ++repeat) Validate(context, nullptr);
    ExpectValues(full);
    Receive(Packet(2,20,20,0,0));
    ExpectValues(partial);
    Receive(Packet(3,20,20,0,0));
    ExpectValues(partial);
    Receive(Packet(1,0,0,0,0));
    ExpectValues(zeros);
    Receive(Packet(0,0,0,0,0));
    ExpectValues(zeros);
    Receive(Packet(4,0x1234,0x2345,0x3456,0x4567));
    const int authority[] = {0x1234,0x2345,0x3456,0x4567};
    ExpectValues(authority);
    for (int badValue : {-1,-32768}) {
        Receive(Packet(4,badValue,20,20,20));
        ExpectValues(zeros);
    }
    packet[6] = 2;
    Receive(packet);
    ExpectValues(zeros);
    packet[6] = 1; packet[7] = 5;
    Receive(packet);
    ExpectValues(zeros);
    packet[7] = 4;
    for (unsigned long size = 0; size <= 20; ++size) {
        if (size == 16) continue;
        packet.resize(20);
        Require(SetItemBonus::HandlePacket(packet.data(), size) == (size >= 6), "strict internal packet length");
    }
    const int refreshes = refreshCount;
    Require(!SetItemBonus::HandlePacket(ordinary, sizeof(ordinary)) && refreshCount == refreshes, "non-owned packet does not refresh twice");
    Receive(Packet(4,20,20,20,20));
    unsigned char login[6]{};
    Require(!SetItemBonus::HandlePacket(login, sizeof(login)) && !state.pending && !state.context, "opcode zero resets before native login");
    Validate(context, nullptr);
    ExpectValues(zeros);
}

void LifecycleTests() {
    auto packet = Packet(4,20,20,20,20);
    const int full[] = {20,20,20,20}, zeros[] = {0,0,0,0};
    const int refreshes = refreshCount;
    std::thread worker([&packet] { Require(Receive(packet), "worker can queue snapshot"); });
    worker.join();
    Require(refreshCount == refreshes && state.pending, "no native stat call from worker thread");
    SetItemBonus::HandlePacket(ordinary, sizeof(ordinary));
    Require(refreshCount == refreshes + 1 && !state.pending, "one deferred game-thread refresh");
    ExpectValues(full);
    InitializeStats(otherSecondary);
    const int reads = readCount, writes = writeCount;
    ApplyBonus(otherSecondary);
    Require(readCount == reads && writeCount == writes, "foreign SecondaryStat untouched");
    Set(context, 0x20B8, static_cast<void*>(otherCharacter));
    InitializeStats(context + 0x2134);
    ApplyBonus(context + 0x2134);
    ExpectValues(zeros);
    Require(!state.context && !state.pending, "changed character invalidates bound snapshot");
    Set(context, 0x20B8, static_cast<void*>(character));
    *reinterpret_cast<void**>(Native(0x00BE7918)) = nullptr;
    Receive(packet);
    Require(state.pending, "not-ready context queues snapshot");
    unsigned char login[6]{};
    SetItemBonus::HandlePacket(login, sizeof(login));
    *reinterpret_cast<void**>(Native(0x00BE7918)) = context;
    SetItemBonus::HandlePacket(ordinary, sizeof(ordinary));
    Require(!state.pending, "fresh login discards pending old snapshot");
    nestedRefresh = true;
    const int beforeNested = refreshCount;
    const int beforeTooltip = SetItemTooltip::refreshCalls;
    Receive(packet);
    Require(refreshCount == beforeNested + 1, "Validate reentrancy guarded");
    Require(SetItemTooltip::refreshCalls == beforeTooltip + 1, "one tooltip refresh follows the game-thread stat refresh");
    nestedRefresh = false;
    ExpectValues(full);
}

__declspec(naked) void StatTail() {
    __asm {
        pop ecx
        ret
    }
}
__declspec(naked) void WithNativeArgument() {
    __asm {
        push 7777h
        jmp BeforeEquipment
    }
}
void GateTests() {
    Jump(statResume, StatTail);
    InitializeStats(context + 0x2134);
    int storage[64]{};
    auto registers = RegistersFor(storage + 32);
    registers.esi = reinterpret_cast<DWORD>(context + 0x2134);
    const auto original = registers;
    Invoke(WithNativeArgument, &registers);
    Require(storage[22] == 0x1111 && registers.eax == 0xFFFFFFFF, "MOV and OR replayed after preserved helper call");
    Require(registers.ecx == 0x7777, "original outstanding argument left for continuation POP");
    Require(registers.ebx == original.ebx && registers.edx == original.edx && registers.esi == original.esi
        && registers.edi == original.edi && registers.ebp == original.ebp, "pre-equipment gate register preservation");
    Require((registers.flags & 0x8C5) == 0x84, "OR EAX,-1 flags replay");
    const int full[] = {20,20,20,20};
    ExpectValues(full);
}
}

void ClientLog::Append(Component, const char*, ...) { ++logCount; }

int main(int argc, char** argv) {
    Require(argc == 2, "EXE path required");
    HMODULE image = LoadImage(argv[1]);
    Bytes(0x0077F875, "\x59\x89\x45\x0c\x83\xf8\xcd", 7);
    InstallTests(image);
    testThread = GetCurrentThreadId();
    *reinterpret_cast<void**>(Native(0x00BE7918)) = context;
    Set(context, 0x20B8, static_cast<void*>(character));
    Jump(Native(0x00416563), reinterpret_cast<void*>(&ReadInt));
    Jump(Native(0x004165B1), reinterpret_cast<void*>(&WriteInt));
    Jump(Native(0x00A0843C), reinterpret_cast<void*>(&Validate));
    ApplyBonus(context + 0x2134);
    PacketTests();
    LifecycleTests();
    GateTests();
    std::puts("PASS: on-disk signatures, strict 0x1011 LE parsing, reset, game-thread refresh, secure ABI/checksum, local-only totals, no double counting and x86 stack/flags");
    return 0;
}
