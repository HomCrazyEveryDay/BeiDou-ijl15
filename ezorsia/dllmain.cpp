// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "NMCO.h"
#include "ijl15.h"
#include "INIReader.h"
#include "ReplacementFuncs.h"
#include "D3D8DisplayModeHook.h"
#include <comutil.h>
#include "BossHP.h"
#include "AranComboUi.h"
#include "HpMpAlert.h"
#include "StackedBuffIcons.h"
#include "SelectCharMacFix.h"
#include "MovementKeyHook.h"
#include "NpcShopCurrency.h"
#include "CrashReporter.h"
#include "ClientCrashFixes.h"
#include "RefreshRateTrace.h"
#include "LauncherGate.h"
#include <wincrypt.h>

enum class ExeVerifyResult {
	Ok,
	GetExePathFailed,
	OpenExeFailed,
	CryptoInitFailed,
	ReadFailed,
	HashFailed,
	HashMismatch,
};

struct ExeVerifyInfo {
	WCHAR exePath[MAX_PATH]{};
	BYTE actualHash[32]{};
	bool hasActualHash = false;
	DWORD lastError = 0;
};

static DWORD g_TextGlyphCodepointReturn = 0x00842505;
static DWORD g_QuestTextGlyphCodepointReturn = 0x00881C54;
static DWORD g_FocusStanceAnimationReturn = 0x00958ADD;
static DWORD g_BoomerangStepAirborneCheckReturn = 0x00950C4D;
static DWORD g_BoomerangStepAirborneAllowed = 0x00950C53;
static DWORD g_BoomerangStepPositionReturn = 0x00950DC1;
static DWORD g_BoomerangStepPositionFail = 0x00950AE8;
static DWORD g_AssassinateNoChargeReturn = 0x00790312;
static DWORD g_DarkSightItemUseCheck = 0x0094FA45;
static DWORD g_AntidoteUseAllowed = 0x00A094BE;
static DWORD g_AntidoteUseRejected = 0x00A0954B;
static DWORD g_AntidoteHotkeyCheckReturn = 0x0094F873;
static DWORD g_AntidoteHotkeyAllowed = 0x0094F879;
static DWORD g_HurricaneMovementCheck = 0x0095F914;
static DWORD g_HurricaneMovementCheckReturn = 0x009CBF13;
static DWORD g_HurricaneSetMovementInput = 0x009B7B4A;
static DWORD g_HurricaneSetMovementInputReturn = 0x009CC0DF;
static DWORD g_BerserkActivationDamageReturn = 0x00A293B7;
static DWORD g_BerserkActiveDamageExit = 0x00A29449;
static DWORD g_BerserkDeactivate = 0x00A29419;
static DWORD g_GetTemporaryStatValue = 0x00725194;
static DWORD g_BattleshipStatPanelSpeedReturn = 0x008C41D6;
static DWORD g_BattleshipPhysicsSpeedReturn = 0x0094D7E3;

// The native cash-item dispatcher opens the AP/SP reset window locally for
// these two server-defined items. Route them through the existing scripted
// item protocol instead, so the server can show a confirmation dialog.
static DWORD g_ScriptedResetItemNativeContinue = 0x00A0A6CB;
static DWORD g_ScriptedResetItemCleanupReturn = 0x00A0EAAC;

namespace {
constexpr int kAllApResetItemId = 5050100;
constexpr int kAllSpResetItemId = 5051001;

struct ScriptedResetOutPacket {
	int Loopback;
	union {
		unsigned char* Data;
		void* Unk;
		unsigned short* Header;
	};
	unsigned long Size;
	unsigned int Offset;
	int EncryptedByShanda;
};

using ScriptedResetSendPacket = void(__fastcall*)(void*, void*, ScriptedResetOutPacket*);

static void __stdcall SendScriptedResetItemPacket(int itemPosition, int itemId)
{
	if (itemId != kAllApResetItemId && itemId != kAllSpResetItemId) {
		return;
	}

	void* socket = *reinterpret_cast<void**>(0x00BE7914);
	if (socket == nullptr) {
		return;
	}

	unsigned char payload[12]{};
	*reinterpret_cast<unsigned short*>(payload) = 0x4E;
	*reinterpret_cast<unsigned int*>(payload + 2) = 0;
	*reinterpret_cast<unsigned short*>(payload + 6) = static_cast<unsigned short>(itemPosition);
	*reinterpret_cast<unsigned int*>(payload + 8) = static_cast<unsigned int>(itemId);

	ScriptedResetOutPacket packet{};
	packet.Data = payload;
	packet.Size = sizeof(payload);
	auto send = reinterpret_cast<ScriptedResetSendPacket>(0x0049637B);

	send(socket, nullptr, &packet);
}
}

__declspec(naked) void RedirectScriptedResetItemCave()
{
	__asm {
		// Replay the original item-id load and packet receiver setup for all
		// normal items. The overwritten bytes are 7 bytes at 00A0A6C4.
		mov esi, dword ptr [ebp + 0Ch]
		cmp esi, 5050100
		je scriptedReset
		cmp esi, 5051001
		je scriptedReset

		push esi
		lea ecx, [ebp - 38h]
		jmp dword ptr [g_ScriptedResetItemNativeContinue]

	scriptedReset:
		push esi
		push dword ptr [ebp + 8]
		call SendScriptedResetItemPacket
		jmp dword ptr [g_ScriptedResetItemCleanupReturn]
	}
}

static void InstallScriptedResetItemRedirect()
{
	Memory::CodeCave(RedirectScriptedResetItemCave, 0x00A0A6C4, 7);
}

static void PatchApResetMinimum(DWORD comparisonAddress, unsigned short nativeMinimum)
{
	constexpr unsigned short serverMinimum = 4;
	const auto* instruction = reinterpret_cast<const unsigned char*>(comparisonAddress);
	const DWORD immediateAddress = comparisonAddress + 2;
	if (instruction[0] == 0x66 && instruction[1] == 0x3D
		&& *reinterpret_cast<const unsigned short*>(immediateAddress) == nativeMinimum) {
		Memory::WriteShort(immediateAddress, serverMinimum);
	}
}

static void InstallApResetStatLimits()
{
	// Match the server's universal four-point floor instead of the native
	// first-job stat requirements used to enable the decrement buttons.
	PatchApResetMinimum(0x008CC10B, 35);
	PatchApResetMinimum(0x008CC13D, 25);
	PatchApResetMinimum(0x008CC166, 20);
	PatchApResetMinimum(0x008CC196, 20);

	constexpr DWORD capInstructionAddress = 0x008CC2C4;
	constexpr int nativeCap = 999;
	constexpr int serverCap = 32767;
	if (*reinterpret_cast<const unsigned char*>(capInstructionAddress) == 0xBB
		&& *reinterpret_cast<const int*>(capInstructionAddress + 1) == nativeCap) {
		Memory::WriteInt(capInstructionAddress + 1, serverCap);
	}
}

__declspec(naked) void BoomerangStepIgnoreAirborneCheckCave()
{
	__asm {
		cmp eax, 040684Fh
		je allowAirborne
		cmp dword ptr[edi + 110h], 0
		jmp dword ptr[g_BoomerangStepAirborneCheckReturn]

	allowAirborne:
		jmp dword ptr[g_BoomerangStepAirborneAllowed]
	}
}

__declspec(naked) void BoomerangStepIgnoreTerrainAndAirCave()
{
	__asm {
		test eax, eax
		jne continueAttack
		cmp dword ptr[ebp - 10h], 040684Fh
		jne failAttack

		mov eax, [ebx + 4]
		lea ecx, [ebx + 4]
		call dword ptr[eax + 10h]
		test eax, eax
		je failAttack
		mov ecx, [eax]
		mov [ebp - 0B0h], ecx
		mov ecx, [eax + 4]
		mov [ebp - 0ACh], ecx

	continueAttack:
		jmp dword ptr[g_BoomerangStepPositionReturn]

	failAttack:
		jmp dword ptr[g_BoomerangStepPositionFail]
	}
}

static void InstallBoomerangStepIgnoreTerrainAndAir()
{
	// Preserve normal movement when a valid endpoint exists; otherwise cast at the current position.
	Memory::CodeCave(BoomerangStepIgnoreAirborneCheckCave, 0x00950C46, 7);
	Memory::CodeCave(BoomerangStepIgnoreTerrainAndAirCave, 0x00950DB9, 8);
}

__declspec(naked) void AssassinateNoChargeCave()
{
	__asm {
		jmp dword ptr[g_AssassinateNoChargeReturn]
	}
}

static void InstallAssassinateNoCharge()
{
	// Skip the original charge-time multiplier without applying any replacement multiplier.
	Memory::CodeCave(AssassinateNoChargeCave, 0x0079028F, 9);
}

__declspec(naked) void AllowAntidoteDuringDarkSightCave()
{
	__asm {
		cmp dword ptr[ebp + 0Ch], 2050000
		je allowed
		cmp dword ptr[ebp + 0Ch], 2050004
		je allowed

		mov ecx, dword ptr ds:[00BEBF98h]
		call dword ptr[g_DarkSightItemUseCheck]
		test eax, eax
		jne rejected

	allowed:
		jmp dword ptr[g_AntidoteUseAllowed]

	rejected:
		jmp dword ptr[g_AntidoteUseRejected]
	}
}

__declspec(naked) void AllowAntidoteHotkeyDuringDarkSightCave()
{
	__asm {
		cmp dword ptr[esi + 1], 2050000
		je allowed
		cmp dword ptr[esi + 1], 2050004
		je allowed

		mov ecx, dword ptr[ebp - 1Ch]
		call dword ptr[g_DarkSightItemUseCheck]
		test eax, eax
		jmp dword ptr[g_AntidoteHotkeyCheckReturn]

	allowed:
		jmp dword ptr[g_AntidoteHotkeyAllowed]
	}
}

static void InstallAntidoteDuringDarkSight()
{
	// Allow status-curing potions through both item-use paths; all other validations remain native.
	Memory::CodeCave(AllowAntidoteDuringDarkSightCave, 0x00A094AB, 19);
	Memory::CodeCave(AllowAntidoteHotkeyDuringDarkSightCave, 0x0094F869, 10);
}

static int ReadClientMovementKey(int virtualKey)
{
	using GetKeyState = int(__thiscall*)(void*, int);

	__try {
		void* input = *reinterpret_cast<void**>(0x00BEC33C);
		return input && reinterpret_cast<GetKeyState>(0x0059A25A)(input, virtualKey) != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static void SetHurricaneAvatarFacing(DWORD user, DWORD facing)
{
	using SetOneTimeAction = void(__thiscall*)(void*, int);

	const DWORD avatarAddress = user + 0x88;
	void* avatar = reinterpret_cast<void*>(avatarAddress);
	DWORD& moveAction = *reinterpret_cast<DWORD*>(user + 0x570);
	const int channelAction = *reinterpret_cast<int*>(avatarAddress + 0x4EC);

	moveAction = (moveAction & ~1u) | facing;

	// Rebuild the exact pose already selected by the client; action IDs are not
	// interchangeable with the Character.wz root indexes used during diagnosis.
	if (channelAction >= 0) {
		reinterpret_cast<SetOneTimeAction>(0x004571AB)(avatar, channelAction);
		*reinterpret_cast<int*>(user + 0xB54) = 0;
		*reinterpret_cast<int*>(user + 0xB58) = 0x7FFFFFFF;
	}
}

static void SetRapidFireAvatarFacing(DWORD user, DWORD facing)
{
	using SetOneTimeAction = void(__thiscall*)(void*, int);
	constexpr DWORD rapidFireChannelAction = 0x63;

	void* avatar = reinterpret_cast<void*>(user + 0x88);
	DWORD& moveAction = *reinterpret_cast<DWORD*>(user + 0x570);
	moveAction = (moveAction & ~1u) | facing;

	// Rebuild the native channel pose for the new facing and keep it on its held frame.
	reinterpret_cast<SetOneTimeAction>(0x004571AB)(avatar, rapidFireChannelAction);
	*reinterpret_cast<int*>(user + 0xB54) = 0;
	*reinterpret_cast<int*>(user + 0xB58) = 0x7FFFFFFF;
}

static void __stdcall ApplyHurricaneMovementInput(DWORD user, int* horizontal, int* vertical)
{
	const int clientHorizontal = ReadClientMovementKey(VK_RIGHT) - ReadClientMovementKey(VK_LEFT);
	const DWORD skillId = *reinterpret_cast<DWORD*>(user + 0x2AE8);

	if (clientHorizontal != 0) {
		const DWORD desiredFacing = clientHorizontal < 0 ? 1 : 0;
		if ((*reinterpret_cast<DWORD*>(user + 0x570) & 1) != desiredFacing) {
			if (skillId == 5221004) {
				SetRapidFireAvatarFacing(user, desiredFacing);
			}
			else {
				SetHurricaneAvatarFacing(user, desiredFacing);
			}
		}
		*horizontal = clientHorizontal;
	}
}

__declspec(naked) void AllowHurricaneMovementCave()
{
	__asm {
		cmp dword ptr[esi + 2AE8h], 02F9F6Ch
		je allowed
		cmp dword ptr[esi + 2AE8h], 04FAA8Ch
		je allowed

		mov ecx, esi
		call dword ptr[g_HurricaneMovementCheck]
		jmp dword ptr[g_HurricaneMovementCheckReturn]

	allowed:
		xor eax, eax
		jmp dword ptr[g_HurricaneMovementCheckReturn]
	}
}

__declspec(naked) void ApplyHurricaneMovementInputCave()
{
	__asm {
		cmp dword ptr[esi + 2AE8h], 02F9F6Ch
		je applyMovementInput
		cmp dword ptr[esi + 2AE8h], 04FAA8Ch
		jne setMovementInput

	applyMovementInput:
		pushfd
		pushad
		lea eax, [ebp - 4]
		push eax
		lea eax, [ebp - 8]
		push eax
		push esi
		call ApplyHurricaneMovementInput
		popad
		popfd

	setMovementInput:
		push dword ptr[ebp - 4]
		mov ecx, edi
		push dword ptr[ebp - 8]
		call dword ptr[g_HurricaneSetMovementInput]
		jmp dword ptr[g_HurricaneSetMovementInputReturn]
	}
}

static void InstallHurricaneMovement()
{
	// Keep native movement checks for every other state, and mirror Hurricane's live layers without resetting it.
	Memory::CodeCave(AllowHurricaneMovementCave, 0x009CBF0C, 7);
	Memory::CodeCave(ApplyHurricaneMovementInputCave, 0x009CC0D2, 13);
}

__declspec(naked) void SuperOctopusAttackCadenceCave()
{
	__asm {
		cmp dword ptr[esp + 4], 04FA6A2h
		jne defaultCadence
		mov eax, 01F4h
		ret

	defaultCadence:
		mov eax, 0BB8h
		cmp dword ptr[esp + 4], 04F837Ah
		jne done
		mov eax, 05DCh

	done:
		ret
	}
}

static void InstallSuperOctopusAttackCadence()
{
	// The client hardcodes Super Octopus to 1500 ms and ignores its WZ animation delays for attack scheduling.
	Memory::CodeCave(SuperOctopusAttackCadenceCave, 0x00765047, 31);
}

__declspec(naked) void AddBattleshipStatPanelSpeedCave()
{
	__asm {
		mov ecx, dword ptr[ebp - 3Ch]
		mov eax, dword ptr[ecx + 14h]
		push ecx
		push eax
		mov ecx, dword ptr[edi + 0CBCh]
		call dword ptr[g_GetTemporaryStatValue]
		cmp dword ptr[eax], 1932000
		pop eax
		pop ecx
		jne done
		add eax, 30

	done:
		jmp dword ptr[g_BattleshipStatPanelSpeedReturn]
	}
}

__declspec(naked) void AddBattleshipPhysicsSpeedCave()
{
	__asm {
		mov edx, dword ptr[ebp - 18h]
		mov eax, dword ptr[edx + 14h]
		push edx
		push eax
		mov ecx, dword ptr[edi + 0CBCh]
		call dword ptr[g_GetTemporaryStatValue]
		cmp dword ptr[eax], 1932000
		pop eax
		pop edx
		jne done
		add eax, 30

	done:
		jmp dword ptr[g_BattleshipPhysicsSpeedReturn]
	}
}

static void InstallBattleshipMovementSpeed()
{
	// Battleship uses its mount speed directly, bypassing the normal SPEED temporary stat.
	Memory::CodeCave(AddBattleshipStatPanelSpeedCave, 0x008C41D0, 6);
	Memory::CodeCave(AddBattleshipPhysicsSpeedCave, 0x0094D7DD, 6);
}

static int __stdcall CalculateBerserkDamage(int skillLevel, int hp, int maxHp)
{
	if (skillLevel <= 0 || skillLevel > 30 || maxHp <= 0) {
		return 0;
	}

	const long long scaledHp = static_cast<long long>(hp) * 100;
	const long long scaledMaxHp = maxHp;
	const int threshold = 45 + skillLevel;
	const int startDamage = 30 + skillLevel * 2;
	const int maxDamage = 30 + skillLevel * 4;
	if (scaledHp > scaledMaxHp * threshold) {
		return 0;
	}

	if (threshold > 50 && scaledHp >= scaledMaxHp * 50) {
		const int midDamage = 30 + skillLevel * 3;
		return startDamage + static_cast<int>(
			(scaledMaxHp * threshold - scaledHp) * (midDamage - startDamage)
			/ (scaledMaxHp * (threshold - 50)));
	}
	if (scaledHp >= scaledMaxHp * 30) {
		const int segmentStartHp = threshold > 50 ? 50 : threshold;
		const int segmentStartDamage = threshold > 50 ? 30 + skillLevel * 3 : startDamage;
		return segmentStartDamage + static_cast<int>(
			(scaledMaxHp * segmentStartHp - scaledHp) * (maxDamage - segmentStartDamage)
			/ (scaledMaxHp * (segmentStartHp - 30)));
	}
	return maxDamage;
}

__declspec(naked) void SetBerserkActivationDamageCave()
{
	__asm {
		movsx ecx, word ptr[ebp - 14h]
		push dword ptr[ebp - 10h]
		push ecx
		push ebx
		call CalculateBerserkDamage
		mov dword ptr[esi + 372Ch], eax
		jmp dword ptr[g_BerserkActivationDamageReturn]
	}
}

__declspec(naked) void UpdateActiveBerserkDamageCave()
{
	__asm {
		movsx ecx, di
		cmp ecx, eax
		jg deactivate

		push dword ptr[ebp - 14h]
		push ecx
		push ebx
		call CalculateBerserkDamage
		mov dword ptr[esi + 372Ch], eax

		jmp dword ptr[g_BerserkActiveDamageExit]

	deactivate:
		jmp dword ptr[g_BerserkDeactivate]
	}
}

static void InstallProgressiveBerserkDamage()
{
	// Every level gains a defined HP threshold and scales to its cap at 30% HP.
	Memory::CodeCave(SetBerserkActivationDamageCave, 0x00A293B1, 6);
	Memory::CodeCave(UpdateActiveBerserkDamageCave, 0x00A29412, 7);
}

static void InstallRushWithoutTargetRequirement()
{
	// This branch is already limited to the three warrior Rush skill IDs.
	Memory::FillBytes(0x00952E2F, 0x90, 6);
}

__declspec(naked) void FocusStanceAnimationCave()
{
	__asm {
		// EAX is the current job ID minus 132 at this point in the native Stance handler.
		cmp eax, 168 // 300 - 132
		je focus
		cmp eax, 178 // 310 - 132
		je focus
		cmp eax, 179 // 311 - 132
		je focus
		cmp eax, 180 // 312 - 132
		je focus
		cmp eax, 188 // 320 - 132
		je focus
		cmp eax, 189 // 321 - 132
		je focus
		cmp eax, 190 // 322 - 132
		je focus
		cmp eax, 390 // 522 - 132
		je focus
		cmp eax, 1980 // 2112 - 132
		je aran

		xor esi, esi
		jmp dword ptr[g_FocusStanceAnimationReturn]

	focus:
		mov esi, 3001003
		jmp dword ptr[g_FocusStanceAnimationReturn]

	aran:
		mov esi, 21121003
		jmp dword ptr[g_FocusStanceAnimationReturn]
	}
}

static void InstallFocusStanceAnimation()
{
	// Extend the native Stance success animation mapping without changing its client-side roll.
	Memory::CodeCave(FocusStanceAnimationCave, 0x00958AB8, 5);
}

__declspec(naked) void TextGlyphCodepointGuardCave()
{
	__asm {
		mov si, word ptr[eax + ecx * 2]
		cmp si, 0738Ch
		jne done
		// U+738C is missing from the text glyph table; reuse a glyph that the same table resolves.
		mov si, 03E8h

	done:
		push 20h
		jmp dword ptr[g_TextGlyphCodepointReturn]
	}
}

static void InstallTextGlyphCodepointGuard()
{
	Memory::CodeCave(TextGlyphCodepointGuardCave, 0x008424FF, 6);
}

__declspec(naked) void QuestTextGlyphCodepointGuardCave()
{
	__asm {
		mov si, word ptr[eax + ecx * 2]
		cmp si, 0738Ch
		jne done
		mov si, 03E8h

	done:
		push 20h
		jmp dword ptr[g_QuestTextGlyphCodepointReturn]
	}
}

static void InstallQuestTextGlyphCodepointGuard()
{
	Memory::CodeCave(QuestTextGlyphCodepointGuardCave, 0x00881C4E, 6);
}

static const char* ExeVerifyResultName(ExeVerifyResult result)
{
	switch (result) {
	case ExeVerifyResult::Ok: return "Ok";
	case ExeVerifyResult::GetExePathFailed: return "GetExePathFailed";
	case ExeVerifyResult::OpenExeFailed: return "OpenExeFailed";
	case ExeVerifyResult::CryptoInitFailed: return "CryptoInitFailed";
	case ExeVerifyResult::ReadFailed: return "ReadFailed";
	case ExeVerifyResult::HashFailed: return "HashFailed";
	case ExeVerifyResult::HashMismatch: return "HashMismatch";
	default: return "Unknown";
	}
}

static void BytesToHex(const BYTE bytes[32], char hex[65])
{
	static const char digits[] = "0123456789ABCDEF";
	for (int i = 0; i < 32; i++) {
		hex[i * 2] = digits[bytes[i] >> 4];
		hex[i * 2 + 1] = digits[bytes[i] & 0x0F];
	}
	hex[64] = '\0';
}

static void WriteLogText(HANDLE file, const char* text)
{
	DWORD written = 0;
	WriteFile(file, text, lstrlenA(text), &written, nullptr);
}

static void WriteExeVerifyLog(ExeVerifyResult result, const ExeVerifyInfo& info)
{
	WCHAR logPath[MAX_PATH]{};
	if (info.exePath[0] != L'\0') {
		lstrcpynW(logPath, info.exePath, MAX_PATH);
	}
	else if (GetModuleFileNameW(nullptr, logPath, MAX_PATH) == 0) {
		lstrcpynW(logPath, L"ijl15_verify.log", MAX_PATH);
	}

	int slash = -1;
	for (int i = lstrlenW(logPath) - 1; i >= 0; i--) {
		if (logPath[i] == L'\\' || logPath[i] == L'/') {
			slash = i;
			break;
		}
	}

	if (slash >= 0) {
		logPath[slash + 1] = L'\0';
		lstrcpynW(logPath + slash + 1, L"ijl15_verify.log", MAX_PATH - slash - 1);
	}
	else {
		lstrcpynW(logPath, L"ijl15_verify.log", MAX_PATH);
	}

	HANDLE file = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}

	char line[256]{};
	WriteLogText(file, "ijl15 verify failed\r\n");
	wsprintfA(line, "reason=%s\r\n", ExeVerifyResultName(result));
	WriteLogText(file, line);
	wsprintfA(line, "lastError=%lu\r\n", info.lastError);
	WriteLogText(file, line);

	if (info.exePath[0] != L'\0') {
		char exePathUtf8[MAX_PATH * 3]{};
		WideCharToMultiByte(CP_UTF8, 0, info.exePath, -1, exePathUtf8, sizeof(exePathUtf8), nullptr, nullptr);
		WriteLogText(file, "exe=");
		WriteLogText(file, exePathUtf8);
		WriteLogText(file, "\r\n");
	}

	if (info.hasActualHash) {
		char hashHex[65]{};
		BytesToHex(info.actualHash, hashHex);
		WriteLogText(file, "actualSha256=");
		WriteLogText(file, hashHex);
		WriteLogText(file, "\r\n");
	}

	CloseHandle(file);
}

static ExeVerifyResult ReadCurrentExeSha256(ExeVerifyInfo& info)
{
	if (GetModuleFileNameW(nullptr, info.exePath, MAX_PATH) == 0) {
		info.lastError = GetLastError();
		return ExeVerifyResult::GetExePathFailed;
	}

	HANDLE file = CreateFileW(info.exePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		info.lastError = GetLastError();
		return ExeVerifyResult::OpenExeFailed;
	}

	HCRYPTPROV provider = 0;
	HCRYPTHASH hasher = 0;
	ExeVerifyResult result = ExeVerifyResult::Ok;

	if (CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)
		&& CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hasher)) {
		BYTE buffer[8192]{};
		DWORD bytesRead = 0;
		while (true) {
			if (!ReadFile(file, buffer, sizeof(buffer), &bytesRead, nullptr)) {
				info.lastError = GetLastError();
				result = ExeVerifyResult::ReadFailed;
				break;
			}
			if (bytesRead == 0) {
				break;
			}
			if (!CryptHashData(hasher, buffer, bytesRead, 0)) {
				info.lastError = GetLastError();
				result = ExeVerifyResult::HashFailed;
				break;
			}
		}

		if (result == ExeVerifyResult::Ok) {
			DWORD hashLen = 32;
			if (!CryptGetHashParam(hasher, HP_HASHVAL, info.actualHash, &hashLen, 0)) {
				info.lastError = GetLastError();
				result = ExeVerifyResult::HashFailed;
			}
			else if (hashLen != 32) {
				info.lastError = ERROR_BAD_LENGTH;
				result = ExeVerifyResult::HashFailed;
			}
			else {
				info.hasActualHash = true;
			}
		}
	}
	else {
		info.lastError = GetLastError();
		result = ExeVerifyResult::CryptoInitFailed;
	}

	if (hasher) {
		CryptDestroyHash(hasher);
	}
	if (provider) {
		CryptReleaseContext(provider, 0);
	}
	CloseHandle(file);

	return result;
}

static void DecodeExpectedExeSha256(BYTE expected[32])
{
	static const BYTE encoded[32] = {
		0xCB, 0xAB, 0xE7, 0x56, 0x7E, 0x6C, 0x52, 0x5A,
		0x5E, 0x53, 0x0E, 0x93, 0x3B, 0x47, 0xD4, 0x28,
		0x12, 0x8D, 0x18, 0x37, 0xBB, 0x1F, 0x75, 0x17,
		0x85, 0xEA, 0xB8, 0x17, 0x0B, 0x63, 0x98, 0xC8
	};

	for (int i = 0; i < 32; i++) {
		expected[i] = encoded[i] ^ static_cast<BYTE>(0xA7 + i * 0x3D);
	}
}

static ExeVerifyResult VerifyCurrentExe(ExeVerifyInfo& info)
{
	BYTE expected[32]{};
	const ExeVerifyResult readResult = ReadCurrentExeSha256(info);
	if (readResult != ExeVerifyResult::Ok) {
		return readResult;
	}

	DecodeExpectedExeSha256(expected);
	DWORD diff = 0;
	for (int i = 0; i < 32; i++) {
		diff |= info.actualHash[i] ^ expected[i];
	}
	return diff == 0 ? ExeVerifyResult::Ok : ExeVerifyResult::HashMismatch;
}

static bool ParseIpv4Address(const std::string& value, unsigned int parts[4])
{
	char tail = '\0';
	const int matched = sscanf_s(value.c_str(), "%u.%u.%u.%u%c", &parts[0], &parts[1], &parts[2], &parts[3], &tail, 1);
	if (matched != 4) {
		return false;
	}

	for (int i = 0; i < 4; i++) {
		if (parts[i] > 255) {
			return false;
		}
	}
	return true;
}

static bool IsAllowedLocalEndpointAddress(const std::string& value, std::string& normalized)
{
	if (_stricmp(value.c_str(), "localhost") == 0) {
		normalized = "127.0.0.1";
		return true;
	}

	unsigned int parts[4]{};
	if (!ParseIpv4Address(value, parts)) {
		return false;
	}

	const bool loopback = parts[0] == 127;
	const bool private10 = parts[0] == 10;
	const bool private172 = parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31;
	const bool private192 = parts[0] == 192 && parts[1] == 168;
	if (!loopback && !private10 && !private172 && !private192) {
		return false;
	}

	normalized = value;
	return true;
}

static void ApplyLocalEndpointOverride(const INIReader& reader)
{
	if (!reader.GetBoolean("dev", "enableLocalEndpointOverride", false)) {
		return;
	}

	std::string endpoint = reader.Get("dev", "ServerIP_Address", "127.0.0.1");
	std::string normalizedEndpoint;
	if (!IsAllowedLocalEndpointAddress(endpoint, normalizedEndpoint)) {
		return;
	}

	const long port = reader.GetInteger("dev", "serverIP_Port", Client::serverIP_Port);
	if (port <= 0 || port > 65535) {
		return;
	}

	Client::ServerIP_Address = normalizedEndpoint;
	Client::serverIP_Port = static_cast<int>(port);
}

struct ResolutionEnvironment {
	int desktopWidth = 0;
	int desktopHeight = 0;
	int workAreaWidth = 0;
	int workAreaHeight = 0;
};

static ResolutionEnvironment ReadResolutionEnvironment()
{
	// Resolution is intentionally not normalized: config.ini is the source of truth.
	ResolutionEnvironment environment{};
	environment.desktopWidth = GetSystemMetrics(SM_CXSCREEN);
	environment.desktopHeight = GetSystemMetrics(SM_CYSCREEN);
	environment.workAreaWidth = environment.desktopWidth;
	environment.workAreaHeight = environment.desktopHeight;
	RECT workArea{};
	if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
		environment.workAreaWidth = workArea.right - workArea.left;
		environment.workAreaHeight = workArea.bottom - workArea.top;
	}

	return environment;
}

static void WriteStartupLog(
	int configParseError,
	int requestedWidth,
	int requestedHeight,
	const ResolutionEnvironment& environment)
{
	if (!Client::enableStartupLog) {
		return;
	}

	WCHAR logPath[MAX_PATH]{};
	if (GetModuleFileNameW(nullptr, logPath, MAX_PATH) == 0) {
		lstrcpynW(logPath, L"ijl15_startup.log", MAX_PATH);
	}

	int slash = -1;
	for (int i = lstrlenW(logPath) - 1; i >= 0; i--) {
		if (logPath[i] == L'\\' || logPath[i] == L'/') {
			slash = i;
			break;
		}
	}

	if (slash >= 0) {
		logPath[slash + 1] = L'\0';
		lstrcpynW(logPath + slash + 1, L"ijl15_startup.log", MAX_PATH - slash - 1);
	}
	else {
		lstrcpynW(logPath, L"ijl15_startup.log", MAX_PATH);
	}

	HANDLE file = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}

	char line[256]{};
	WriteLogText(file, "ijl15 startup\r\n");
	wsprintfA(line, "configParseError=%d\r\n", configParseError);
	WriteLogText(file, line);
	wsprintfA(line, "requestedResolution=%dx%d\r\n", requestedWidth, requestedHeight);
	WriteLogText(file, line);
	wsprintfA(line, "desktopResolution=%dx%d\r\n", environment.desktopWidth, environment.desktopHeight);
	WriteLogText(file, line);
	wsprintfA(line, "workArea=%dx%d\r\n", environment.workAreaWidth, environment.workAreaHeight);
	WriteLogText(file, line);
	wsprintfA(line, "finalResolution=%dx%d\r\n", Client::m_nGameWidth, Client::m_nGameHeight);
	WriteLogText(file, line);
	WriteLogText(file, "resolutionFallback=false\r\n");
	WriteLogText(file, "resolutionFallbackReason=disabled\r\n");
	wsprintfA(line, "serverEndpoint=%s:%d\r\n", Client::ServerIP_Address.c_str(), Client::serverIP_Port);
	WriteLogText(file, line);
	CloseHandle(file);
}

void CreateConsole() {
	AllocConsole();
	FILE* stream;
	freopen_s(&stream, "CONOUT$", "w", stdout); //CONOUT$
}

namespace
{
	constexpr DWORD ProcessEntryAddress = 0x00A63FF3;
	constexpr DWORD ProcessEntryContinueAddress = 0x00A63FF8;
	constexpr BYTE ExpectedProcessEntryBytes[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
	DWORD g_processEntryContinueAddress = ProcessEntryContinueAddress;

	void InitializeClientAtProcessEntry()
	{
		if (!LauncherGate::Authorize()) {
			if (LauncherGate::ShouldShowUnauthorizedLaunchMessage()) {
				LauncherGate::ShowUnauthorizedLaunchMessage();
			}
			ExitProcess(ERROR_ACCESS_DENIED);
		}

		ExeVerifyInfo verifyInfo{};
		const ExeVerifyResult verifyResult = VerifyCurrentExe(verifyInfo);
		if (verifyResult != ExeVerifyResult::Ok) {
			WriteExeVerifyLog(verifyResult, verifyInfo);
			MessageBoxW(
				nullptr,
				L"\u5BA2\u6237\u7AEF\u6587\u4EF6\u6821\u9A8C\u5931\u8D25\uFF0C\u8BF7\u8FD0\u884C\u9010\u68A6\u542F\u52A8\u5668\u4FEE\u590D\u5BA2\u6237\u7AEF\u3002",
				L"\u65E0\u6CD5\u542F\u52A8\u6E38\u620F",
				MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
			ExitProcess(ERROR_BAD_EXE_FORMAT);
		}

		//CreateConsole();	//console for devs, use this to log stuff if you want

		// config.ini exposes compatibility/debug settings plus an opt-in local/LAN endpoint override for testing.
		// Other patch behavior stays in code defaults.
		INIReader reader("config.ini");
		bool enableCrashDump = true;
		bool enableCrashTrace = true;
		std::string crashDumpType = "mini";
		bool enableStackedBuffIconLog = false;
		const int configParseError = reader.ParseError();
		if (configParseError == 0) {
			// Resolution and IME are local client compatibility settings.
			Client::m_nGameWidth = reader.GetInteger("general", "width", 1280);
			Client::m_nGameHeight = reader.GetInteger("general", "height", 720);
			Client::imeType = reader.GetInteger("general", "imeType", 1);
			Client::enableMovementKeyRebind = reader.GetBoolean("general", "enableMovementKeyRebind", false);
			enableCrashDump = reader.GetBoolean("debug", "enableCrashDump", true);
			enableCrashTrace = reader.GetBoolean("dev", "enableCrashTrace", true);
			crashDumpType = reader.Get("debug", "crashDumpType", "mini");
			Client::enableStartupLog = reader.GetBoolean("debug", "enableStartupLog", false);
			enableStackedBuffIconLog = reader.GetBoolean("debug", "enableStackedBuffIconLog", false);
			ApplyLocalEndpointOverride(reader);
		}
		const int requestedWidth = Client::m_nGameWidth;
		const int requestedHeight = Client::m_nGameHeight;
		const ResolutionEnvironment resolutionEnvironment = ReadResolutionEnvironment();
		CrashReporter::Install(enableCrashDump, crashDumpType, enableCrashTrace);
		ClientCrashFixes::Install();
		WriteStartupLog(configParseError, requestedWidth, requestedHeight, resolutionEnvironment);

		Hook_CreateMutexA(true); //multiclient //ty darter, angel, and alias!
		HookCreateWindowExA(true); //default ezorsia
		// Gr2D_DX8 validates windowed resolutions against D3D8 display modes before CreateDevice.
		D3D8DisplayModeHook::Install();
		HookGetModuleFileName(true); //default ezorsia
		HookPcCreateObject_IWzResMan(true);
		HookPcCreateObject_IWzNameSpace(true);
		HookPcCreateObject_IWzFileSystem(true);
		HookCWvsApp__Dir_BackSlashToSlash(true);
		HookCWvsApp__Dir_upDir(true);
		Hookbstr_ctor(true);
		HookAvatarLayerBuild(true);
		HookIWzFileSystem__Init(true);
		HookIWzNameSpace__Mount(true);
		HookCWvsApp__InitializeResMan(false); //experimental //ty to all the contributors of the ragezone release: Client load .img instead of .wz v62~v92
		Hook_StringPool__GetString(true); //hook stringpool modification //ty !! popcorn //ty darter
		Hook_lpfn_NextLevel(true);
		HookSaveGlobal(true);
		HookHpMpAlertRecv(true);
		StackedBuffIcons::Install(enableStackedBuffIconLog);
		HookSelectCharMacFix(true);
		//Hook_get_unknown(true);
		//Hook_get_resource_object(true); //helper function hooks  //ty teto for helping me get started
		//Hook_com_ptr_t_IWzProperty__ctor(true);
		//Hook_com_ptr_t_IWzProperty__dtor(true);

		Client::UpdateGameStartup();

		std::cout << "Applying resolution " << Client::m_nGameWidth << "x" << Client::m_nGameHeight << std::endl;
		Client::UpdateResolution();
		Client::FixMouseWheel();
		Client::Chinese();
		Client::LongQuickSlot();
		InstallTextGlyphCodepointGuard();
		InstallQuestTextGlyphCodepointGuard();
		InstallFocusStanceAnimation();
		InstallBoomerangStepIgnoreTerrainAndAir();
		InstallAssassinateNoCharge();
		InstallAntidoteDuringDarkSight();
		InstallHurricaneMovement();
		InstallSuperOctopusAttackCadence();
		InstallBattleshipMovementSpeed();
		InstallProgressiveBerserkDamage();
		InstallRushWithoutTargetRequirement();
		InstallScriptedResetItemRedirect();
		InstallApResetStatLimits();
		if (Client::enableMovementKeyRebind) {
			MovementKeyHook::Hook(true);
			Client::MovementKeyRebind();
		}
		Client::FixDateFormat();
		Client::FixItemType();
		Client::JumpCap();
		Client::FixChatPosHook();
		Client::NoPassword();
		NpcShopCurrency::Install();
		Client::MoreHook();
		AranComboUi::Install();
		BossHP::Hook();
		Client::WorldMap();
		RefreshRateTrace::Install();
		Client::DeleteChar();
		std::cout << "GetModuleFileName hook created" << std::endl;
		ijl15::CreateHook(); //NMCO::CreateHook();
		std::cout << "NMCO hook initialized" << std::endl;
	}

	__declspec(naked) void ProcessEntryHook()
	{
		__asm {
			pushfd
			pushad
			call InitializeClientAtProcessEntry
			popad
			popfd

			// Replay the five bytes replaced at BeiDou.exe's process entry point.
			push ebp
			mov ebp, esp
			push -1
			jmp dword ptr[g_processEntryContinueAddress]
		}
	}

	bool InstallProcessEntryHook()
	{
		auto* const entryPoint = reinterpret_cast<BYTE*>(ProcessEntryAddress);
		MEMORY_BASIC_INFORMATION memoryInfo{};
		if (VirtualQuery(entryPoint, &memoryInfo, sizeof(memoryInfo)) != sizeof(memoryInfo)
			|| memoryInfo.State != MEM_COMMIT
			|| (memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
			return false;
		}

		if (memcmp(entryPoint, ExpectedProcessEntryBytes, sizeof(ExpectedProcessEntryBytes)) != 0) {
			return false;
		}

		BYTE patch[sizeof(ExpectedProcessEntryBytes)]{ 0xE9 };
		const INT_PTR relativeJump = reinterpret_cast<INT_PTR>(&ProcessEntryHook)
			- (reinterpret_cast<INT_PTR>(entryPoint) + sizeof(patch));
		*reinterpret_cast<LONG*>(&patch[1]) = static_cast<LONG>(relativeJump);

		DWORD oldProtect = 0;
		if (!VirtualProtect(entryPoint, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			return false;
		}

		memcpy(entryPoint, patch, sizeof(patch));
		FlushInstructionCache(GetCurrentProcess(), entryPoint, sizeof(patch));

		DWORD ignoredProtect = 0;
		VirtualProtect(entryPoint, sizeof(patch), oldProtect, &ignoredProtect);
		return true;
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
		// Keep loader-lock work to a five-byte process-entry jump. Authorization, UI,
		// configuration parsing and all existing hooks run after the loader calls the EXE entry point.
		return InstallProcessEntryHook() ? TRUE : FALSE;
	}

	return TRUE;
}





