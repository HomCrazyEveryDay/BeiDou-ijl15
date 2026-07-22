// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "NMCO.h"
#include "ijl15.h"
#include "INIReader.h"
#include "ReplacementFuncs.h"
#include <comutil.h>
#include "BossHP.h"
#include "HpMpAlert.h"
#include "SelectCharMacFix.h"
#include "MovementKeyHook.h"
#include "NpcShopCurrency.h"
#include "CrashReporter.h"
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

void CreateConsole() {
	AllocConsole();
	FILE* stream;
	freopen_s(&stream, "CONOUT$", "w", stdout); //CONOUT$
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
	{
		ExeVerifyInfo verifyInfo{};
		const ExeVerifyResult verifyResult = VerifyCurrentExe(verifyInfo);
		if (verifyResult != ExeVerifyResult::Ok) {
			WriteExeVerifyLog(verifyResult, verifyInfo);
			return FALSE;
		}

		//CreateConsole();	//console for devs, use this to log stuff if you want

		// config.ini only exposes local compatibility/debug settings; server endpoint is locked in Client.cpp.
		// Other patch behavior stays in code defaults.
		INIReader reader("config.ini");
		bool enableCrashDump = true;
		std::string crashDumpType = "mini";
		if (reader.ParseError() == 0) {
			// Resolution and IME are local client compatibility settings.
			Client::m_nGameWidth = reader.GetInteger("general", "width", 1280);
			Client::m_nGameHeight = reader.GetInteger("general", "height", 720);
			Client::imeType = reader.GetInteger("general", "imeType", 1);
			Client::enableMovementKeyRebind = reader.GetBoolean("general", "enableMovementKeyRebind", false);
			enableCrashDump = reader.GetBoolean("debug", "enableCrashDump", true);
			crashDumpType = reader.Get("debug", "crashDumpType", "mini");
		}

		Hook_CreateMutexA(true); //multiclient //ty darter, angel, and alias!
		HookCreateWindowExA(true); //default ezorsia
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
		BossHP::Hook();
		Client::WorldMap();
		Client::RefreshRate();
		Client::DeleteChar();
		std::cout << "GetModuleFileName hook created" << std::endl;
		ijl15::CreateHook(); //NMCO::CreateHook();
		CrashReporter::Install(enableCrashDump, crashDumpType);
		std::cout << "NMCO hook initialized" << std::endl;
		break;
	}
	default: break;
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}





