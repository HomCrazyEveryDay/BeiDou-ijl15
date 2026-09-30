// Compiled recovery hooks with mapped v83 reference bytes and fake native dependencies.
// No client process or server interaction.
#define DEATH_DIALOG_DIAGNOSTICS_TEST
#include "../ezorsia/DeathDialogDiagnostics.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace DeathDialogDiagnostics;
static std::vector<std::string> logs;
void ClientLog::Append(Component, const char* format, ...) {
    char text[2048]; va_list args; va_start(args, format);
    vsnprintf(text, sizeof(text), format, args); va_end(args); logs.emplace_back(text);
}
static BYTE userData[0x4000]{}, remoteData[0x4000]{}, contextData[0x4000]{}, appData[0x100]{}, characterData[0x100]{};
static int lastForce, actionCalls, updateCalls, createCalls, cancelCalls, armCalls;
static int hookAttempts, failHook, detachCalls;
static bool throwCreate, nullCreate, throwAction;
static DWORD& Word(void* base, size_t offset) { return *reinterpret_cast<DWORD*>(static_cast<BYTE*>(base) + offset); }
static void __fastcall ActionStub(void* avatar, void*, int requested, int force) {
    lastForce = force; ++actionCalls;
    if (throwAction) RaiseException(0xE0000124, 0, 0, nullptr);
    const bool unchanged = Word(avatar, 0x4E8) == requested && !force;
    Word(avatar, 0x4E8) = requested;
    if (avatar == userData + 0x88 && Dead(requested)) {
        Word(userData, 0x1F9C) = 1;
        if (!unchanged && !force && !Word(contextData, 0x3520)) Word(contextData, 0x3520) = Word(appData, 0x18);
    }
}
static void __fastcall UpdateStub(void*, void*) { ++updateCalls; }
static void* __cdecl CreateStub() {
    ++createCalls;
    if (throwCreate) RaiseException(0xE0000123, 0, 0, nullptr);
    return *reinterpret_cast<void**>(Native(0xBF1028)) = nullCreate ? nullptr : reinterpret_cast<void*>(0x12345678);
}
static void __fastcall CancelStub(void* context, void*) {
    ++cancelCalls; Word(context, 0x3520) = 0; *reinterpret_cast<void**>(Native(0xBF1028)) = nullptr;
}
static void __fastcall ArmStub(void* context, void*) {
    ++armCalls;
    if (!Word(context, 0x3520)) Word(context, 0x3520) = Word(appData, 0x18);
}
bool Memory::SetHook(bool attach, void** target, void*) {
    if (attach && ++hookAttempts == failHook) return false;
    if (!attach) ++detachCalls;
    if (target == reinterpret_cast<void**>(&action)) *target = attach ? reinterpret_cast<void*>(ActionStub) : reinterpret_cast<void*>(Native(0x92ECD1));
    else if (target == reinterpret_cast<void**>(&update)) *target = attach ? reinterpret_cast<void*>(UpdateStub) : reinterpret_cast<void*>(Native(0xA03350));
    else if (target == reinterpret_cast<void**>(&create)) *target = attach ? reinterpret_cast<void*>(CreateStub) : reinterpret_cast<void*>(Native(0xA14AFC));
    else if (target == reinterpret_cast<void**>(&cancel)) *target = attach ? reinterpret_cast<void*>(CancelStub) : reinterpret_cast<void*>(Native(0xA0670C));
    else assert(false);
    return true;
}
static bool Contains(const char* text) {
    for (const auto& s : logs) if (s.find(text) != std::string::npos) return true;
    return false;
}
static void SetHp(unsigned short hp) {
    BYTE* p = characterData + 0x61;
    p[0] = 0xA7; p[1] = 0x5C; p[2] = p[0] ^ (hp & 255); p[3] = p[1] ^ (hp >> 8);
    DWORD check = 0xBAADF00D;
    for (int i = 0; i < 2; ++i) { check = _rotr(check ^ p[i], 5) + p[i + 2]; }
    memcpy(p + 4, &check, 4);
}
static void Reset() {
    cycle = Cycle{}; budget = LogBudget{}; logs.clear();
    recovery = logging = true;
    actionCalls = updateCalls = createCalls = cancelCalls = armCalls = 0;
    throwCreate = nullCreate = throwAction = false;
    Word(userData, 0x570) = 4; Word(userData, 0x1F9C) = 0;
    Word(contextData, 0x3520) = 0; Word(appData, 0x18) = 100000;
    *reinterpret_cast<void**>(Native(0xBEBF98)) = userData;
    *reinterpret_cast<void**>(Native(0xBF1028)) = nullptr;
    SetHp(0);
}
static void ForcedDeath() { ActionHook(userData + 0x88, nullptr, 18, 1); assert(lastForce == 1); }
static void Poll(DWORD now) { Observe(Read(), now); }
static void AssertCreateException() {
    bool caught = false;
    __try { CreateHook(); }
    __except (GetExceptionCode() == 0xE0000123 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { caught = true; }
    assert(caught && Contains("phase=create_exception") && Contains("exception=E0000123"));
}
static void AssertActionException() {
    bool caught = false;
    __try { ActionHook(userData + 0x88, nullptr, 18, 0); }
    __except (GetExceptionCode() == 0xE0000124 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { caught = true; }
    assert(caught && Contains("phase=action_exception"));
}
int main(int argc, char** argv) {
    assert(argc == 3);
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<char> file((std::istreambuf_iterator<char>(in)), {});
    assert(file.size() > 4096);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(file.data() + dos->e_lfanew);
    assert(nt->OptionalHeader.ImageBase == 0x400000);
    auto base = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(Native(0x400000)),
        nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(base == reinterpret_cast<BYTE*>(Native(0x400000)));
    auto section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(base + section[i].VirtualAddress, file.data() + section[i].PointerToRawData, section[i].SizeOfRawData);
    wchar_t config[MAX_PATH]{};
    assert(MultiByteToWideChar(CP_ACP, 0, argv[2], -1, config, MAX_PATH));
    assert(WritePrivateProfileStringW(L"DeathDialog", L"Logging", L"0", config));
    assert(WritePrivateProfileStringW(L"DeathDialog", L"Recovery", L"0", config));
    assert(Install(config) && hookAttempts == 0);
    assert(WritePrivateProfileStringW(L"DeathDialog", nullptr, nullptr, config));
    assert(WritePrivateProfileStringW(L"DeathDialogTest", L"Mode", L"1", config));
    *reinterpret_cast<BYTE*>(Native(0xA066F3)) ^= 1;
    assert(!Install(config) && hookAttempts == 0);
    *reinterpret_cast<BYTE*>(Native(0xA066F3)) ^= 1;
    failHook = 3;
    assert(!Install(config) && detachCalls == 2);
    hookAttempts = 0; failHook = 0;
    assert(Install(config) && hookAttempts == 4 && recovery && logging);
    assert(Install(config) && hookAttempts == 4);
    assert(Contains("testInjection=removed"));
    arm = reinterpret_cast<Arm>(ArmStub);
    *reinterpret_cast<void**>(Native(0xBE7918)) = contextData;
    *reinterpret_cast<void**>(Native(0xBE7B38)) = appData;
    *reinterpret_cast<void**>(contextData + 0x20B8) = characterData;

    // Normal death keeps the native delay and the legacy Mode=1 cannot inject.
    Reset(); ActionHook(userData + 0x88, nullptr, 18, 0);
    assert(lastForce == 0 && Word(contextData, 0x3520) == 100000);
    Poll(1000); Poll(5000); assert(armCalls == 0);
    assert(CreateHook() && createCalls == 1 && Contains("phase=create_after"));
    CancelHook(contextData, nullptr); Poll(6000); Poll(10000);
    assert(armCalls == 0 && cycle.closed); // no reopening after a revive choice

    // Reproduce the real native forced-first gap; recovery only schedules once.
    Reset(); ForcedDeath(); Poll(1000); Poll(3999); assert(armCalls == 0);
    Poll(4000); assert(armCalls == 1 && Word(contextData, 0x3520) == 100000);
    assert(Contains("phase=recover_missing_timer") && Contains("phase=recover_scheduled"));
    Word(contextData, 0x3520) = 0; Poll(5000); Poll(10000); assert(armCalls == 1);
    ActionHook(userData + 0x88, nullptr, 19, 1); assert(lastForce == 1);
    Poll(14000); assert(armCalls == 1);

    // Stable zero HP + death effect is mandatory; remote actions cannot start it.
    Reset(); ActionHook(remoteData + 0x88, nullptr, 18, 1); Poll(1000); Poll(5000);
    assert(armCalls == 0 && !cycle.active);
    Reset(); ForcedDeath(); SetHp(1); Poll(1000); Poll(5000); assert(armCalls == 0);
    SetHp(30000); assert(Read().valid && Read().hp == 30000);
    SetHp(0); characterData[0x65] ^= 1; assert(!Read().valid);
    Poll(10000); assert(armCalls == 0);
    Reset(); Word(userData, 0x570) = 18; Poll(1000); Poll(5000); assert(armCalls == 0);

    // Even a missed action event can be recovered from stable native state.
    Word(userData, 0x1F9C) = 1; Poll(6000); Poll(9000);
    assert(armCalls == 1 && Contains("phase=observed_dead"));

    // A cancelled pre-dialog timer can recover; a context reset must not reopen.
    Reset(); ActionHook(userData + 0x88, nullptr, 18, 0);
    CancelHook(contextData, nullptr); Poll(1000); Poll(4000); assert(armCalls == 1);
    Reset(); ForcedDeath(); MarkCancel(Read(), reinterpret_cast<void*>(Native(0xA03EC3)));
    Poll(1000); Poll(10000); assert(armCalls == 0 && cycle.closed);
    characterData[0x65] ^= 1; Poll(11000); SetHp(0); Poll(12000); Poll(16000);
    assert(armCalls == 0 && cycle.closed); // unreadable sample cannot forget closure

    // Constructor failure is logged, propagated and never retried in a loop.
    Reset(); ForcedDeath(); nullCreate = true; assert(!CreateHook());
    Poll(1000); Poll(20000); assert(armCalls == 0 && Contains("phase=create_failed"));
    Reset(); ForcedDeath(); throwCreate = true; AssertCreateException();
    Poll(1000); Poll(20000); assert(armCalls == 0);
    Reset(); throwAction = true; AssertActionException();

    // Positive HP ends one episode; a later death on the same objects recovers.
    Reset(); ForcedDeath(); CreateHook(); CancelHook(contextData, nullptr);
    SetHp(1); ActionHook(userData + 0x88, nullptr, 4, 0); assert(!cycle.active);
    SetHp(0); ForcedDeath(); Poll(1000); Poll(4000); assert(armCalls == 1);

    Reset(); ForcedDeath(); recovery = false; Poll(1000); Poll(4000); assert(armCalls == 0);
    cycle.started = 0; Poll(10000); Poll(20000); assert(Contains("phase=timeout"));
    const auto count = logs.size(); Poll(30000); assert(logs.size() == count);
    Reset(); logging = false; ForcedDeath(); Poll(1000); Poll(4000);
    assert(armCalls == 1 && logs.empty()); // fix independent of logging switch

    Reset(); ForcedDeath(); Poll(0xFFFFF800); Poll(0x000003B8); assert(armCalls == 1); // DWORD wrap
    Reset(); lastSample = GetTickCount(); UpdateHook(contextData, nullptr); assert(updateCalls == 1);
    void* slots[5]{}; slots[4] = reinterpret_cast<void*>(0x94A86C);
    assert(ActionOrigin(reinterpret_cast<void*>(Native(0x94A11A)), slots) == slots[4]);

    // Bounded logs retain an independent anomaly budget and recover next hour.
    Reset(); budget.started = GetTickCount();
    for (int i = 0; i < 300; ++i) Record("normal", Read());
    assert(logs.size() == 256);
    Record("late_failure", Read(), true); assert(Contains("late_failure"));
    for (int i = 0; i < 100; ++i) Record("failure", Read(), true);
    assert(logs.size() == 320);
    budget.started = GetTickCount() - 3600000; Record("next_hour", Read()); assert(Contains("next_hour"));
    std::puts("PASS: signatures/rollback, injection removed, normal/forced deaths, guarded single recovery, HP validation, missed entry, cancellation/transition/choice guards, exception evidence, new death cycles, switches, time wrap and renewable log bounds.");
}
