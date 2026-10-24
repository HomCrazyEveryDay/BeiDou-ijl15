#ifdef VISITORS_SHUTTLE_DISPATCH_TEST
#include "../ezorsia/VisitorsMorph.cpp"
#else
#include "../ezorsia/VisitorsShuttle.cpp"
#endif
#include "VisitorsShuttleTest.h"
using namespace TestSupport;

namespace {
int logCount = 0;
#ifndef VISITORS_SHUTTLE_DISPATCH_TEST
int resourceSkill = 0;
bool missing = false;
void* database = reinterpret_cast<void*>(0x12345678);
void* __fastcall Resource(void* instance, void*, int skill) {
    Require(instance == database, "FindSkill thiscall receiver");
    resourceSkill = skill;
    return missing ? nullptr : &resourceSkill;
}

void InstallTests(HMODULE image) {
    for (const auto& changed : patches) {
        auto* site = reinterpret_cast<unsigned char*>(changed.address);
        Require(!std::memcmp(site, changed.expected, changed.size), "all five on-disk patch signatures");
        site[0] ^= 1;
        Require(!VisitorsShuttle::Install(image), "signature mismatch fails closed");
        const int logs = logCount;
        Require(!VisitorsShuttle::Install(image) && logs == logCount, "failure logged once");
        site[0] ^= 1;
        for (const auto& patch : patches)
            Require(!std::memcmp(reinterpret_cast<void*>(patch.address), patch.expected, patch.size), "no partial patch");
    }
    Require(!VisitorsShuttle::Install(nullptr), "wrong image base rejected");
    DWORD ignored;
    for (const auto& patch : patches)
        Require(VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size, PAGE_EXECUTE_READ, &ignored), "test RX pages");
    Require(VisitorsShuttle::Install(image) && VisitorsShuttle::Install(image), "install and idempotence");
    for (const auto& patch : patches) {
        auto* site = reinterpret_cast<unsigned char*>(patch.address);
        Require(site[0] == 0xE9 && site + 5 + *reinterpret_cast<int*>(site + 1) == patch.target, "JMP destination");
        if (patch.size == 6) Require(site[5] == 0x90, "whole instruction NOP padding");
        MEMORY_BASIC_INFORMATION information{};
        VirtualQuery(site, &information, sizeof(information));
        Require(information.Protect == PAGE_EXECUTE_READ, "RX protection restored");
        VirtualProtect(site, patch.size, PAGE_EXECUTE_READWRITE, &ignored);
    }
}

void GateTests() {
    unsigned char user[0x2000]{};
    int storage[64]{};
    int* frame = storage + 32;
    Jump(vehicleResume, Fallback);
    Jump(vehicleAccepted, Accepted);
    Jump(effectResume, Fallback);
    Jump(effectTail, Accepted);
    Jump(restrictionResume, Fallback);
    Jump(restrictionCheck, Accepted);
    Jump(startResume, Fallback);
    Jump(startCount, Accepted);
    Jump(cancelResume, Fallback);
    Jump(cancelCount, Accepted);
    for (int vehicle : {1932000,1932001,1932007,1932036,1932037,1932038}) {
        auto registers = RegistersFor(frame);
        registers.eax = vehicle;
        Invoke(VehicleGate, &registers);
        Require(reached == (vehicle == 1932037 ? 1 : 2) && registers.eax == vehicle, "only OS4 accepted by new gate");
        if (vehicle == 1932001) Require((registers.flags & 0x40) != 0, "original vehicle CMP flags");
    }
    *reinterpret_cast<void**>(Native(0x00BE78DC)) = database;
    Jump(Native(0x0075C755), reinterpret_cast<void*>(&Resource));
    const int jobs[] = {0,112,232,522,900,910,1000,1112,1512,2000,2100,2112,2001,2200,2210,2218};
    for (int job : jobs) {
        const int expected = job == 2001 || job / 100 == 22 ? 20011065 : (job / 1000) * 10000000 + 1065;
        Require(VisitorsShuttle::SkillForJob(job) == expected, "job mapping with Evan precedence");
        frame[2] = 1932037;
        auto registers = RegistersFor(frame);
        registers.eax = job;
        registers.esi = reinterpret_cast<DWORD>(user);
        const auto original = registers;
        Invoke(EffectGate, &registers);
        Require(reached == 1 && resourceSkill == expected && frame[-4] == reinterpret_cast<int>(&resourceSkill), "native skill lookup and shared effect tail");
        Require(*reinterpret_cast<int*>(user + 0x1F4C) == 240, "native special-mount timing");
        Require(!std::memcmp(&registers, &original, sizeof(registers)), "effect gate preserves registers and flags");
    }
    for (int job : {-1,1,999,1999,2002,2201,2219,2999,3000,INT_MAX})
        Require(VisitorsShuttle::SkillForJob(job) == 0, "unknown job rejected");
    missing = true;
    Require(!ShuttleResource(112) && !ShuttleResource(112), "missing skill returns null without substituting Battleship");
    frame[2] = 1932037;
    auto missingRegisters = RegistersFor(frame);
    missingRegisters.eax = 112;
    missingRegisters.esi = reinterpret_cast<DWORD>(user);
    Invoke(EffectGate, &missingRegisters);
    Require(reached == 1 && frame[-4] == 0, "missing resource reaches native null-safe effect tail");
    frame[2] = 1932007;
    frame[-4] = 0x7654;
    auto registers = RegistersFor(frame);
    registers.eax = 112;
    registers.esi = reinterpret_cast<DWORD>(user);
    Invoke(EffectGate, &registers);
    Require(reached == 2 && registers.eax == 1932007 && registers.ebx == 112 && frame[-4] == 0x7654, "non-OS4 replays both instructions");
    for (int skill : {1065,10001065,20001065,20011065,1064,1066,1067,1004,5221006,30001065}) {
        for (void* gate : {reinterpret_cast<void*>(RestrictionGate),reinterpret_cast<void*>(StartGate),reinterpret_cast<void*>(CancelGate)}) {
            auto state = RegistersFor(frame);
            state.eax = skill;
            state.ebx = skill;
            const auto original = state;
            Invoke(gate, &state);
            Require(reached == (VisitorsShuttle::IsSkill(skill) ? 1 : 2), "exact IDs in restrictions and mount/cancel counters");
            Require(!std::memcmp(&state, &original, offsetof(Registers, flags)), "counter gates preserve IDs and registers");
            if (VisitorsShuttle::IsSkill(skill)) Require(state.flags == original.flags, "OS4 flags preserved");
        }
    }
}
#else
void DispatchTests() {
    Bytes(0x009678F9, "\x81\xfe\x07\x04\x00\x00", 6);
    Bytes(0x009675D5, "\xe8\x1c\xa1\xdf\xff", 5);
    Bytes(0x009676AB, "\x83\x7d\xec\x00\x0f\x8e\x3b\x1d\x00\x00", 10);
    Bytes(0x009691AC, "\xff\x75\xe8\x8b\x4d\xf0\xe8\xa0\xcf\xdf\xff", 11);
    Jump(Native(0x009691AC), Accepted);
    Jump(dispatchResume, Fallback);
    int frame[64]{};
    unsigned char user[0x600]{};
    for (int skill : {1065,10001065,20001065,20011065,1064,1066,1067,30001065}) {
        auto registers = RegistersFor(frame + 32);
        registers.esi = skill;
        registers.ebx = reinterpret_cast<DWORD>(user);
        const auto original = registers;
        Invoke(Dispatch, &registers);
        const bool os4 = skill == 1065 || skill == 10001065 || skill == 20001065 || skill == 20011065;
        Require(reached == (os4 ? 1 : 2), "shared Dispatch routes exactly four OS4 IDs");
        Require(!std::memcmp(&registers, &original, offsetof(Registers, flags)), "Dispatch register ABI");
        if (os4) Require(SkillBase(skill) == 0 && VisitorLevel(nullptr, nullptr, skill, nullptr, true) == -1, "OS4 uses native learned level");
    }
    Jump(Native(0x009693F0), Fallback);
    Jump(Native(0x009676B5), Accepted);
    for (int level : {-1,0,1,15}) {
        auto registers = RegistersFor(frame + 32);
        frame[27] = level;
        Invoke(reinterpret_cast<void*>(Native(0x009676AB)), &registers);
        Require(reached == (level > 0 ? 1 : 2), "execute original learned-level rejection without bypass");
    }
}
#endif
}

void ClientLog::Append(Component, const char*, ...) { ++logCount; }

int main(int argc, char** argv) {
    Require(argc == 2, "original EXE path required");
    HMODULE image = LoadImage(argv[1]);
#ifdef VISITORS_SHUTTLE_DISPATCH_TEST
    DispatchTests();
    std::puts("PASS: shared Morph Dispatch, exact OS4 IDs, original learned-level rejection signature and ABI");
#else
    Bytes(0x0075C755, "\xb8\x7c\x3d\xab\x00\xe8\x39\x44\x30\x00", 10);
    Bytes(0x0096A316, "\x83\xbe\xac\x31\x00\x00\x00", 7);
    Bytes(0x0096D8F0, "\x83\xbf\xac\x31\x00\x00\x00", 7);
    Bytes(0x00969FB4, "\x8d\x45\xec\x50\x8b\xce", 6);
    Bytes(0x00936B45, "\x39\x5d\xf0\x0f\x84\x16\x01\x00\x00", 9);
    InstallTests(image);
    GateTests();
    std::puts("PASS: disk signatures, fail-closed installation, RX restore, job mapping, x86 gates and non-OS4 replay");
#endif
    return 0;
}
