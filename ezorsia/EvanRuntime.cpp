#include "stdafx.h"
#include "EvanRuntime.h"
#include <cstring>
#include "EvanKillingWing.h"
#include "EvanDragonVisibility.h"
#include "EvanMountRender.h"
#ifndef EVAN_RUNTIME_TEST
#include "EvanTimingDiagnostics.h"
#include "EvanAttackDiagnostics.h"
#include "ClientLog.h"
#endif

namespace {
#ifdef EVAN_RUNTIME_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
// The existing server derives Mir from the equipped saddle (no inventory
void __cdecl UpdateDragonVisibility(void* dragon) {
    DWORD model = 0;
    const HRESULT result = EvanDragonVisibility::Update(dragon, model);
#ifndef EVAN_RUNTIME_TEST
    static DWORD lastModel = 0xFFFFFFFF;
    static unsigned records = 0;
    static const bool logging = [] { char setting[8]{};
        GetEnvironmentVariableA("BEIDOU_EVAN_MOUNT_LOG", setting, sizeof(setting));
        return setting[0] != '0'; }();
    if (logging && model != lastModel && records < 96) {
        lastModel = model; ++records;
        ClientLog::Append(ClientLog::Component::Trace,
            "event=evan_dragon_visibility model=%lu result=%08lX", model, result);
    }
#endif
}
const DWORD dragonUpdateNative = Native(0x004FF794);
int __cdecl SelectMountBody(int model, int requested, int selected) {
    return EvanMountRender::BodyAction(model, requested, selected);
}
// Select and observe the actual body-loader input after native pose selection.
void __cdecl TraceMountBody(int model, int requested, int bodyAction, int skin) {
#ifndef EVAN_RUNTIME_TEST
    static unsigned records = 0;
    if (model < 1902040 || model > 1902042 || records >= 48) return;
    if (requested != 36 && requested != 37) return;
    char setting[8]{};
    GetEnvironmentVariableA("BEIDOU_EVAN_MOUNT_LOG", setting, sizeof(setting));
    if (setting[0] == '0') return;
    ++records;
    ClientLog::Append(ClientLog::Component::Trace,
        "event=evan_mount_body requested=%d bodyAction=%d model=%d skin=%d", requested, bodyAction, model, skin);
#endif
}
const DWORD mountBodyNative = Native(0x0041272C);
const DWORD mountBodyResume = Native(0x00407A03);
__declspec(naked) void MountBodyTraceGate() {
    __asm {
        pushfd
        pushad
        push dword ptr [ebp-18h]
        push dword ptr [ebp+8]
        push dword ptr [ebp+24h]
        call SelectMountBody
        add esp, 12
        mov [ebp-18h], eax
        // Six native args are already on the stack. Patch arg 1 too;
        // pushad (32) + pushfd (4) are above it.
        mov [esp+24h], eax
        push dword ptr [ebp+0ch]
        push dword ptr [ebp-18h]
        push dword ptr [ebp+8]
        push dword ptr [ebp+24h]
        call TraceMountBody
        add esp, 16
        popad
        popfd
        call dword ptr [mountBodyNative]
        jmp dword ptr [mountBodyResume]
    }
}
const DWORD dragonUpdateResume = Native(0x004FF02A);
__declspec(naked) void DragonVisibilityGate() {
    __asm {
        call dword ptr [dragonUpdateNative]
        pushfd
        pushad
        push ebx
        call UpdateDragonVisibility
        add esp, 4
        popad
        popfd
        jmp dword ptr [dragonUpdateResume]
    }
}
// The existing server derives Mir from the equipped saddle (no inventory
// mount in -18). v83 968BA0, like v84 9A7D35, otherwise requires both slots.
// Validate only Evan here, then rejoin the native field/action/send checks.
int __cdecl EvanSaddleReady(void* characterData) {
    auto saddle = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(characterData) + 0x183);
    return saddle != nullptr;
}
const DWORD mountOriginal = Native(0x00968BA7);
const DWORD mountMissing = Native(0x00968EE2);
const DWORD mountFieldCheck = Native(0x00968D83);
__declspec(naked) void EvanSaddleGate() {
    __asm {
        cmp esi, 20011004
        jne original
        pushad
        push eax
        call EvanSaddleReady
        add esp, 4
        test eax, eax
        popad
        jz missing
        xor edi, edi
        jmp dword ptr [mountFieldCheck]
    missing:
        jmp dword ptr [mountMissing]
    original:
        cmp dword ptr [eax+17bh], 0
        jmp dword ptr [mountOriginal]
    }
}
// CDraggableItem's double-click dispatcher recognizes CUIEquip/CUIPetEquip,
// but omits CUIDragonEquip (RTTI 00BF0E50, vtable 00B38B40).
// Extend only this failed window check, then reuse native UnequipItem.
const DWORD dragonEquipType = Native(0x00BF0E50);
const DWORD dragonEquipClickContinue = Native(0x004F0CE7);
__declspec(naked) void DragonEquipDoubleClick() {
    __asm {
        call dword ptr [eax+48h]
        test eax, eax
        jnz done
        cmp dword ptr [edi+18h], 1
        jne done
        cmp dword ptr [edi+1ch], -1000
        jg done
        cmp dword ptr [edi+1ch], -1003
        jl done
        mov ecx, [edi+24h]
        mov eax, [ecx]
        push dword ptr [dragonEquipType]
        call dword ptr [eax+48h]
    done:
        test eax, eax
        jmp dword ptr [dragonEquipClickContinue]
    }
}
// CDragon's movement tear-off is dragon+4. v83 4FEAA7 chooses the normal
// movement direction from vx; this turns Mir around when the owner is knocked
// backwards without turning. Keep the native move/stand action and trajectory,
// but use the local owner's CAvatar direction for ordinary following only.
// v84 50753A adds direction hysteresis; it still uses vx, so this is an explicit
// follow-facing fix, not a claim that v84 already implements this policy.
const DWORD dragonMoveContinue = Native(0x004FEAAC);
__declspec(naked) void DragonMoveOriginal() {
    __asm {
        push esi
        mov esi, [esp+10h]
        jmp dword ptr [dragonMoveContinue]
    }
}
int __fastcall DragonMoveFacing(void* movement, void*, int vx, int vy, int stance, void* controller) {
    using Calculate = int(__thiscall*)(void*, int, int, int, void*);
    const int result = reinterpret_cast<Calculate>(&DragonMoveOriginal)(movement, vx, vy, stance, controller);
    auto dragon = static_cast<unsigned char*>(movement) - 4;
    void* owner = *reinterpret_cast<void**>(dragon + 0xf8);
    if (!owner || (result >> 1) < 1 || (result >> 1) > 2) return result;
    using IsLocal = int(__thiscall*)(void*);
    const auto table = *reinterpret_cast<void***>(owner);
    if (!reinterpret_cast<IsLocal>(table[3])(owner)) return result;
    // Same secure one-time-action getter used by 4FEB23 / 4FF44F.
    using Decode = int(__cdecl*)(void*, int);
    const int action = reinterpret_cast<Decode>(Native(0x00416563))(
        dragon + 0x98, *reinterpret_cast<int*>(dragon + 0xa0));
    if (action > -1) return result; // Preserve casting, including breath layer flip.
    const int ownerStance = *reinterpret_cast<int*>(static_cast<unsigned char*>(owner) + 0x570);
    return (result & ~1) | (ownerStance & 1); // CAvatar+4E8, owner+88.
}
// v84 treats 5620006..8 as mastery books (0x4F959A). Keep the v83
// native skill-book packet/lock lifecycle and accept their CASH slots.
const DWORD bookClickReturn = Native(0x004F06CB);
const DWORD bookSendReturn = Native(0x00A0A1D2);
__declspec(naked) void BookClick() {
    __asm {
        cmp eax, 562
        jne original
        cmp edx, 6
        jl original
        cmp edx, 8
        jg original
        cmp eax, eax
        jmp dword ptr [bookClickReturn]
    original:
        cmp eax, 228
        jmp dword ptr [bookClickReturn]
    }
}
__declspec(naked) void BookSend() {
    __asm {
        cmp eax, 562
        jne original
        cmp edx, 6
        jl original
        cmp edx, 8
        jg original
        cmp eax, eax
        jmp dword ptr [bookSendReturn]
    original:
        cmp eax, 228
        jmp dword ptr [bookSendReturn]
    }
}
// GMS 084 0x4F0AD2 adds these three cash-book skills to the shared
// master-level predicate. Pair with the server's skill-list field predicate.
const DWORD masteryReturn = Native(0x004E8F09);
__declspec(naked) void EvanMastery() {
    __asm {
        mov eax, [esp+4]
        cmp eax, 22111001
        je hasMastery
        cmp eax, 22141002
        je hasMastery
        cmp eax, 22140000
        je hasMastery
        cdq
        jmp dword ptr [masteryReturn]
    hasMastery:
        mov eax, 1
        ret
    }
}
// v84 Blaze uses the projectile/chain branch (0x992D53), not the
// rectangular lt/rb branch. The v83 prototype ID is now Flame Wheel:
// replace it with Blaze, rather than admitting both into the ball branch.
const DWORD blazeChainReturn = Native(0x00955EE4);
const DWORD blazeBallReturn = Native(0x00955F25);
const DWORD blazeOriginReturn = Native(0x00955FB8);
// v83 Illusion starts its action at 0x956339, AFTER the hit-delay calculation
// at 0x955C2D. Unlike v84 (0x991FAF), the avatar duration here still belongs
// to the previous action. Reconstruct the current duration using the same
// per-frame truncation as CAvatar::PrepareAction (0x454776..0x454837).
// Keep target selection, native hit scheduling and the original WZ frames.
int __cdecl IllusionDuration(const unsigned char* action, int speed, int previous) {
    const int* frames = *reinterpret_cast<const int* const*>(action + 0x14);
    if (!frames) return previous;
    const unsigned count = reinterpret_cast<const unsigned*>(frames)[-1];
    if (!count || count > 1024) return previous;
    if (speed < 2) speed = 2;
    if (speed > 10) speed = 10;
    long long total = 0;
    for (unsigned i = 0; i < count; ++i) {
        const int delay = frames[i * 8 + 2]; // 32-byte ACTIONFRAME, delay +8
        if (delay < 0) return previous; // Native loader already took abs(delay).
        total += static_cast<long long>(delay) * (speed + 10) / 16;
    }
    const int duration = total > 0 && total <= 0x7fffffff ? static_cast<int>(total) : previous;
#ifndef EVAN_RUNTIME_TEST
    EvanTimingDiagnostics::HitDelay(previous, duration, speed,
        *reinterpret_cast<const int*>(action+0x10), *reinterpret_cast<const int*>(action+0x0c));
#endif
    return duration;
}
const DWORD illusionDurationReturn = Native(0x00955C34);
__declspec(naked) void IllusionTiming() {
    __asm {
        mov eax, [ecx+ebx+4f8h]
        pushfd
        cmp dword ptr [ebp-14h], 22171002
        jne finished
        push ecx
        push edx
        push eax
        push dword ptr [ebp-98h]
        push esi
        call IllusionDuration
        add esp, 0ch
        pop edx
        pop ecx
    finished:
        popfd
        jmp dword ptr [illusionDurationReturn]
    }
}
// CMob::AddDamage queues each Illusion hit separately. GMS084 0x68129C
// uses 0/60/180/420ms, whereas the v83 prototype uses 0/90/270/630ms.
// The five-hit skill uses 0/42/84/126/168ms, retaining the previous final-hit
// offset and the 800ms avatar/dragon actions. The native four-entry table
// returns zero for hit index 4, so replace its result at the queue boundary.
// Observe the actual queue boundary, not just the earlier base-delay estimate.
void __cdecl IllusionQueueTrace(int index, int base, int offset) {
#ifndef EVAN_RUNTIME_TEST
    const DWORD now = reinterpret_cast<DWORD(__cdecl*)()>(0x987257)();
    EvanTimingDiagnostics::QueuedHit(index, offset,
        static_cast<int>(static_cast<DWORD>(base) + offset - now));
#endif
}
const DWORD illusionQueueReturn = Native(0x0066B101);
__declspec(naked) void IllusionQueue() {
    __asm {
        pushfd
        push ecx
        mov ecx, [ebp+24h]
        cmp ecx, 4
        ja nativeOffset
        imul eax, ecx, 42
    nativeOffset:
        pop ecx
        pushad
        push eax
        push dword ptr [ebp+10h]
        push dword ptr [ebp+24h]
        call IllusionQueueTrace
        add esp, 0ch
        popad
        popfd
        mov ecx, [ebp+10h]
        add ecx, eax
        jmp dword ptr [illusionQueueReturn]
    }
}
__declspec(naked) void BlazeChain() {
    __asm {
        cmp dword ptr [ebp-14h], 22181001
        jmp dword ptr [blazeChainReturn]
    }
}
__declspec(naked) void BlazeBall() {
    __asm {
        cmp dword ptr [ebp-14h], 22181001
        jmp dword ptr [blazeBallReturn]
    }
}
__declspec(naked) void BlazeOrigin() {
    __asm {
        cmp dword ptr [ebp-14h], 22181001
        jmp dword ptr [blazeOriginReturn]
    }
}
// Both lookup functions explicitly discard the 22xx and 2001 skill families.
// Keep the native skill map, level checks and all non-Evan handling intact.
// The target footer is ignored by the server. Keep every damage integer and
// packet length unchanged; EC01 identifies an authoritative 15-line crit mask.
unsigned __cdecl CriticalFooter(int skill, int count, const int* critical, unsigned original) {
    if (skill / 1000000 != 22 || count < 1 || count > 15) return original;
    unsigned result = 0xec010000;
    for (int i = 0; i < count; ++i) if (critical[i]) result |= 1u << i;
    return result;
}
const DWORD footerNative = Native(0x006711AC);
const DWORD footerReturn = Native(0x00957070);
__declspec(naked) void MagicCriticalFooter() {
    __asm {
        mov ecx, [esi]
        call dword ptr [footerNative]
        pushfd
        push ecx
        push edx
        push eax
        lea eax, [esi+54h]
        push eax
        push dword ptr [ebp-64h]
        push dword ptr [ebp-14h]
        call CriticalFooter
        add esp, 10h
        pop edx
        pop ecx
        popfd
        jmp dword ptr [footerReturn]
    }
}
// Area eligibility is independent of chain-ball eligibility. Preserve Flame
// Wheel's v83 target-loop exemption when migrating Blaze's ball ID; otherwise
// a Zakum arm crossing the projectile origin terminates all remaining hits.
// v84 9C29D4/9C29DB also adds Magic Flare and Earthquake to this gate.
// Dark Fog's eight-target trace confirms the same area-path requirement.
const DWORD remoteAreaReturn = Native(0x00982757);
void __cdecl ObserveGeometry(int* frame, int origin) {
#ifndef EVAN_RUNTIME_TEST
    const DWORD saved = GetLastError();
    __try {
        EvanAttackDiagnostics::RemoteGeometry(frame[-5], frame[5], origin,
            frame[-15], frame[-13], frame[-14], frame[-12], frame[-4]);
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
    SetLastError(saved);
#endif
}
__declspec(naked) void RemoteArea() {
    __asm {
        pushfd
        pushad
        push ebx
        push ebp
        call ObserveGeometry
        add esp,8
        popad
        popfd
        cmp eax, 22181002
        je finished
        cmp eax, 22171003
        je finished
        cmp eax, 22131000
        je finished
        cmp eax, 22161001
        je finished
        cmp eax, 22181001
    finished:
        jmp dword ptr [remoteAreaReturn]
    }
}
const DWORD remoteActionNative = Native(0x0092EDB2), remoteActionReturn = Native(0x00980765);
void __cdecl ObserveAction(int skill, int action, int accepted) {
#ifndef EVAN_RUNTIME_TEST
    const DWORD saved = GetLastError();
    __try { EvanAttackDiagnostics::RemoteAction(skill, action, accepted); }
    __except(EXCEPTION_EXECUTE_HANDLER) { }
    SetLastError(saved);
#endif
}
__declspec(naked) void RemoteAction() {
    __asm {
        call dword ptr [remoteActionNative]
        pushfd
        pushad
        push eax
        push [ebp-20h]
        push [ebp-10h]
        call ObserveAction
        add esp,12
        popad
        popfd
        jmp dword ptr [remoteActionReturn]
    }
}
// v84 0067BFE1 adds Phantom Imprint to CMob's native status-effect map.
// MobStat decoding and damage already support it in v83; only this visual
// registration is absent. Native reconciliation handles refresh and removal.
const DWORD imprintReturn = Native(0x006660C3);
const DWORD statusMapInsert = Native(0x0047CCB7);
__declspec(naked) void ImprintEffect() {
    __asm {
        cmp [esi+370h],edi
        je finished
        lea eax,[ebp-1ch]
        push eax
        lea eax,[esi+374h]
        push eax
        lea ecx,[ebp-38h]
        mov [ebp-1ch],edi
        call dword ptr [statusMapInsert]
    finished:
        mov eax,[esi+0bch]
        jmp dword ptr [imprintReturn]
    }
}
const DWORD beaconClassify=Native(0x0076662D), beaconReturn=Native(0x00666116);
void __cdecl ObserveBeacon(void* mob,int skill,int retained) {
#ifndef EVAN_RUNTIME_TEST
    const DWORD saved=GetLastError();
    __try { EvanAttackDiagnostics::BeaconRetention(mob,skill,retained); }
    __except(EXCEPTION_EXECUTE_HANDLER) { }
    SetLastError(saved);
#endif
}
__declspec(naked) void BeaconCleanupProbe() {
    __asm {
        call dword ptr [beaconClassify]
        pushfd
        pushad
        push eax
        push [ebx]
        push esi
        call ObserveBeacon
        add esp,12
        popad
        popfd
        jmp dword ptr [beaconReturn]
    }
}
struct Patch {
    DWORD address;
    unsigned char before[8];
    unsigned char after[8];
    size_t size;
};
// GMS084 CField_Afrien (5789CA) adds no storage or custom rendering to CField;
// its only behavior override is GetFieldType() == 28 (5789F1). Keep the native
// base constructor/Init and original map user/look data, including all pixels.
DWORD memoryFieldVtable[15]{};
int __fastcall MemoryFieldType(void*, void*) { return 28; }
void __cdecl SetMemoryFieldType(void* field, int type) {
    if (!field || type != 28) return;
    auto original = *reinterpret_cast<DWORD**>(field);
    if (!memoryFieldVtable[1]) {
        std::memcpy(memoryFieldVtable, original - 1, sizeof(memoryFieldVtable));
        memoryFieldVtable[8] = reinterpret_cast<DWORD>(&MemoryFieldType);
    }
    *reinterpret_cast<DWORD**>(field) = memoryFieldVtable + 1;
}
const DWORD baseFieldConstructor = Native(0x00528DBC);
const DWORD baseFieldConstructorReturn = Native(0x00527ECA);
__declspec(naked) void MemoryFieldConstructorGate() {
    __asm {
        call dword ptr [baseFieldConstructor]
        pushfd
        pushad
        push esi
        push eax
        call SetMemoryFieldType
        add esp, 8
        popad
        popfd
        jmp dword ptr [baseFieldConstructorReturn]
    }
}
int __cdecl MemoryBlocksSkill(void* field, void* user) {
    if (!field || !user) return 0;
    using GetInt = int (__thiscall*)(void*);
    auto fieldMethods = *reinterpret_cast<DWORD**>(field);
    if (reinterpret_cast<GetInt>(fieldMethods[7])(field) != 28) return 0;
    auto userMethods = *reinterpret_cast<DWORD**>(user);
    int job = reinterpret_cast<GetInt>(userMethods[16])(user);
    return job == 2001 || job / 100 == 22;
}
const DWORD nativeFieldSkillCheck = Native(0x00537C7F);
const DWORD nativeFieldSkillReturn = Native(0x0096700E);
__declspec(naked) void MemorySkillGate() {
    __asm {
        call dword ptr [nativeFieldSkillCheck]
        test eax, eax
        jnz done
        pushad
        push edi
        push dword ptr [ebp-5ch]
        call MemoryBlocksSkill
        add esp, 8
        mov [esp+1ch], eax
        popad
    done:
        jmp dword ptr [nativeFieldSkillReturn]
    }
}
Patch patches[] = {
    {Native(0x00761717), {0x0f,0x84,0xd7,0,0,0}, {0x90,0x90,0x90,0x90,0x90,0x90}, 6},
    {Native(0x00761723), {0x0f,0x84,0xcb,0,0,0}, {0x90,0x90,0x90,0x90,0x90,0x90}, 6},
    {Native(0x0075c779), {0x74,0x08}, {0x90,0x90}, 2},
    {Native(0x0075c781), {0x75,0x04}, {0xeb,0x04}, 2},
    {Native(0x00955E22), {0x89,0x74,0x52,0x01}, {0x7b,0x4d,0x52,0x01}, 4},
    {Native(0x00955EDD), {0x81,0x7d,0xec,0x7b,0x4d,0x52,0x01}, {0xe9,0,0,0,0,0x90,0x90}, 7},
    {Native(0x00955F1E), {0x81,0x7d,0xec,0x7b,0x4d,0x52,0x01}, {0xe9,0,0,0,0,0x90,0x90}, 7},
    {Native(0x00955FB1), {0x81,0x7d,0xec,0x7b,0x4d,0x52,0x01}, {0xe9,0,0,0,0,0x90,0x90}, 7},
    {Native(0x004E8F04), {0x8b,0x44,0x24,0x04,0x99}, {0xe9,0,0,0,0}, 5},
    {Native(0x004F06C6), {0x3d,0xe4,0,0,0}, {0xe9,0,0,0,0}, 5},
    {Native(0x00A0A1CD), {0x3d,0xe4,0,0,0}, {0xe9,0,0,0,0}, 5},
    {Native(0x00955C2D), {0x8b,0x84,0x19,0xf8,0x04,0,0}, {0xe9,0,0,0,0,0x90,0x90}, 7},
    {Native(0x0066B0DD), {0xc7,0x45,0xe4,0x5a,0,0,0}, {0xc7,0x45,0xe4,0x18,0,0,0}, 7},
    {Native(0x0066B0E4), {0xc7,0x45,0xe8,0x0e,0x01,0,0}, {0xc7,0x45,0xe8,0x48,0,0,0}, 7},
    {Native(0x0066B0EB), {0xc7,0x45,0xec,0x76,0x02,0,0}, {0xc7,0x45,0xec,0xa8,0,0,0}, 7},
    {Native(0x0066B0FC), {0x8b,0x4d,0x10,0x03,0xc8}, {0xe9,0,0,0,0}, 5},
    // Remote magic effects still used the v83 Blaze prototype (now Flame Wheel).
    // Match v84 9C29CD / 9C2C7E / 9C2CFC / 9C2DF1 / 9C334A:
    // only Blaze owns this chain-ball path; Flame Wheel has no ball resource.
    // Validate whole comparisons, not just immediates, before applying any patch.
    {Native(0x00982752), {0x3d,0x7b,0x4d,0x52,0x01}, {0xe9,0,0,0,0}, 5},
    {Native(0x0098296D), {0x3d,0x7b,0x4d,0x52,0x01}, {0x3d,0x89,0x74,0x52,0x01}, 5},
    {Native(0x009829E8), {0x81,0xf9,0x7b,0x4d,0x52,0x01}, {0x81,0xf9,0x89,0x74,0x52,0x01}, 6},
    {Native(0x00982ACD), {0x81,0x7d,0xec,0x7b,0x4d,0x52,0x01}, {0x81,0x7d,0xec,0x89,0x74,0x52,0x01}, 7},
    {Native(0x00982FAC), {0x81,0x7d,0xec,0x7b,0x4d,0x52,0x01}, {0x81,0x7d,0xec,0x89,0x74,0x52,0x01}, 7},
    {Native(0x00957069), {0x8b,0x0e,0xe8,0x3c,0xa1,0xd1,0xff}, {0xe9,0,0,0,0,0x90,0x90}, 7},
    // v84 9C2E52..9C3109 no longer sends Ice/Fire Breath to the legacy
    // charged-area screen effect. v83 reads skill+9C (null in the observed
    // Fire Breath dump) and passes it to RESMAN via 982C98 -> 4365DB.
    // Earlier ordinary mage branches are retained; default handling follows.
    {Native(0x00982C0C), {0x81,0xf9,0x28,0x8a,0x51,0x01}, {0xe9,0x87,0x01,0,0,0x90}, 6},
    {Native(0x006660BD), {0x8b,0x86,0xbc,0,0,0}, {0xe9,0,0,0,0,0x90}, 6},
    {Native(0x00980760), {0xe8,0x4d,0xe6,0xfa,0xff}, {0xe9,0,0,0,0}, 5},
    {Native(0x00666111), {0xe8,0x17,0x05,0x10,0}, {0xe9,0,0,0,0}, 5},
    {Native(0x004FEAA7), {0x56,0x8b,0x74,0x24,0x10}, {0xe9,0,0,0,0}, 5},
    {Native(0x004F0CE2), {0xff,0x50,0x48,0x85,0xc0}, {0xe9,0,0,0,0}, 5},
    {Native(0x00968BA0), {0x83,0xb8,0x7b,0x01,0,0,0}, {0xe9,0,0,0,0,0x90,0x90}, 7},
    {Native(0x004FF025), {0xe8,0x6a,0x07,0,0}, {0xe9,0,0,0,0}, 5},
    {Native(0x004079FE), {0xe8,0x29,0xad,0,0}, {0xe9,0,0,0,0}, 5},
    {Native(0x00527EC5), {0xe8,0xf2,0x0e,0,0}, {0xe9,0,0,0,0}, 5},
    {Native(0x00967009), {0xe8,0x71,0x0c,0xbd,0xff}, {0xe9,0,0,0,0}, 5}
};
}

#ifdef EVAN_RUNTIME_TEST
extern "C" void TestSetMemoryFieldType(void* field, int type) { SetMemoryFieldType(field, type); }
extern "C" int TestMemoryBlocksSkill(void* field, void* user) { return MemoryBlocksSkill(field, user); }
#endif
bool EvanRuntime::Install() {
    if (!EvanKillingWing::Validate()) return false;
    const DWORD memoryTargets[] = {reinterpret_cast<DWORD>(&MemoryFieldConstructorGate), reinterpret_cast<DWORD>(&MemorySkillGate)};
    for (size_t i = 0; i < 2; ++i) {
        const DWORD displacement = memoryTargets[i] - (patches[31+i].address + 5);
        std::memcpy(patches[31+i].after+1, &displacement, sizeof(displacement));
    }
    const DWORD visibilityDisplacement = reinterpret_cast<DWORD>(&DragonVisibilityGate) - (patches[29].address + 5);
    std::memcpy(patches[29].after+1, &visibilityDisplacement, sizeof(visibilityDisplacement));
    const DWORD bodyDisplacement = reinterpret_cast<DWORD>(&MountBodyTraceGate) - (patches[30].address + 5);
    std::memcpy(patches[30].after+1, &bodyDisplacement, sizeof(bodyDisplacement));
    const DWORD mountDisplacement = reinterpret_cast<DWORD>(&EvanSaddleGate) - (patches[28].address + 5);
    std::memcpy(patches[28].after+1, &mountDisplacement, sizeof(mountDisplacement));
    const DWORD equipClickDisplacement = reinterpret_cast<DWORD>(&DragonEquipDoubleClick) - (patches[27].address + 5);
    std::memcpy(patches[27].after+1, &equipClickDisplacement, sizeof(equipClickDisplacement));
    const DWORD dragonDisplacement = reinterpret_cast<DWORD>(&DragonMoveFacing) - (patches[26].address + 5);
    std::memcpy(patches[26].after+1, &dragonDisplacement, sizeof(dragonDisplacement));
    const DWORD beaconDisplacement=reinterpret_cast<DWORD>(&BeaconCleanupProbe)-(patches[25].address+5);
    std::memcpy(patches[25].after+1,&beaconDisplacement,sizeof(beaconDisplacement));
    const DWORD actionDisplacement = reinterpret_cast<DWORD>(&RemoteAction) - (patches[24].address + 5);
    std::memcpy(patches[24].after+1, &actionDisplacement, sizeof(actionDisplacement));
    const DWORD imprintDisplacement = reinterpret_cast<DWORD>(&ImprintEffect) - (patches[23].address + 5);
    std::memcpy(patches[23].after+1, &imprintDisplacement, sizeof(imprintDisplacement));
    const DWORD areaDisplacement = reinterpret_cast<DWORD>(&RemoteArea) - (patches[16].address + 5);
    std::memcpy(patches[16].after+1, &areaDisplacement, sizeof(areaDisplacement));
    const DWORD footerDisplacement = reinterpret_cast<DWORD>(&MagicCriticalFooter) - (patches[21].address + 5);
    std::memcpy(patches[21].after+1, &footerDisplacement, sizeof(footerDisplacement));
    const DWORD queueDisplacement = reinterpret_cast<DWORD>(&IllusionQueue) - (patches[15].address + 5);
    std::memcpy(patches[15].after+1, &queueDisplacement, sizeof(queueDisplacement));
    const DWORD illusionDisplacement = reinterpret_cast<DWORD>(&IllusionTiming) - (patches[11].address + 5);
    std::memcpy(patches[11].after+1, &illusionDisplacement, sizeof(illusionDisplacement));
    const DWORD bookTargets[] = {reinterpret_cast<DWORD>(&EvanMastery), reinterpret_cast<DWORD>(&BookClick), reinterpret_cast<DWORD>(&BookSend)};
    for (size_t i=0;i<3;++i) {
        const DWORD displacement = bookTargets[i] - (patches[8+i].address + 5);
        std::memcpy(patches[8+i].after+1, &displacement, sizeof(displacement));
    }
    const unsigned char auraDispatch[] = {0xc7,0x86,0x74,0x05,0,0,0x97,0,0,0};
    if (std::memcmp(reinterpret_cast<void*>(Native(0x0096CA98)), auraDispatch, sizeof(auraDispatch))) return false;
    const DWORD destinations[] = {reinterpret_cast<DWORD>(&BlazeChain), reinterpret_cast<DWORD>(&BlazeBall), reinterpret_cast<DWORD>(&BlazeOrigin)};
    for (size_t i=0;i<3;++i) {
        const DWORD displacement = destinations[i] - (patches[5+i].address + 5);
        std::memcpy(patches[5+i].after+1, &displacement, sizeof(displacement));
    }
    for (const auto& patch : patches) {
        if (std::memcmp(reinterpret_cast<const void*>(patch.address), patch.before, patch.size)) return false;
    }
    // Acquire every page before modifying any instruction, so a protection
    // failure cannot leave one skill lookup enabled and the other disabled.
    DWORD previous[sizeof(patches) / sizeof(patches[0])]{};
    size_t acquired = 0;
    for (const auto& patch : patches) {
        if (!VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size,
                PAGE_EXECUTE_READWRITE, &previous[acquired])) {
            for (size_t i = acquired; i > 0; --i) {
                DWORD ignored;
                VirtualProtect(reinterpret_cast<void*>(patches[i-1].address), patches[i-1].size, previous[i-1], &ignored);
            }
            return false;
        }
        ++acquired;
    }
    for (const auto& patch : patches) {
        std::memcpy(reinterpret_cast<void*>(patch.address), patch.after, patch.size);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patch.address), patch.size);
    }
    for (size_t i = acquired; i > 0; --i) {
        DWORD ignored;
        VirtualProtect(reinterpret_cast<void*>(patches[i-1].address), patches[i-1].size, previous[i-1], &ignored);
    }
#ifndef EVAN_RUNTIME_TEST
    EvanTimingDiagnostics::Install();
#endif
    return EvanKillingWing::Install();
}
