#include "stdafx.h"
#include "AranComboUi.h"

namespace
{
	constexpr DWORD kClientTick = 0x00987257;
	constexpr DWORD kOriginalBuildComboUi = 0x00960342;
	constexpr DWORD kOriginalClearComboUi = 0x00960F21;
	constexpr DWORD kClearLayerRef = 0x00428712;

	constexpr DWORD kShowComboBuildCall = 0x009602EA;
	constexpr DWORD kTimedClearCall = 0x0094BDD4;
	constexpr DWORD kComboDigitLayerTtlPatch = 0x00960708 + 1;
	constexpr DWORD kComboTextLayerTtlPatch = 0x0096095B + 1;

	constexpr DWORD kComboCountOffset = 0x3220;
	constexpr DWORD kComboLastShownAtOffset = 0x3224;
	constexpr DWORD kComboCommandVisibleOffset = 0x3228;
	constexpr DWORD kComboDigitLayerBaseOffset = 0x3238;
	constexpr DWORD kComboCommandLeftLayerOffset = 0x324C;
	constexpr DWORD kComboCommandRightLayerOffset = 0x3250;
	constexpr DWORD kComboDigitLayerCount = 5;

	constexpr DWORD kComboUiTtlMs = 9000;

	using ClientTickFunc = DWORD(__cdecl*)();
	using BuildComboUiFunc = void(__fastcall*)(void*, void*);
	using ClearComboUiFunc = void(__fastcall*)(void*, void*);
	using ClearLayerRefFunc = void(__thiscall*)(void*, void*);

	ClientTickFunc ClientTick = reinterpret_cast<ClientTickFunc>(kClientTick);
	BuildComboUiFunc BuildComboUi = reinterpret_cast<BuildComboUiFunc>(kOriginalBuildComboUi);
	ClearComboUiFunc ClearComboUi = reinterpret_cast<ClearComboUiFunc>(kOriginalClearComboUi);
	ClearLayerRefFunc ClearLayerRef = reinterpret_cast<ClearLayerRefFunc>(kClearLayerRef);

	// SHOW_COMBO only carries the current count, so cache the previous display shape to reconcile decay updates.
	DWORD g_lastCommandTier = 0;
	DWORD g_lastDigitCount = 0;

	DWORD ReadDword(unsigned char* base, DWORD offset)
	{
		return *reinterpret_cast<DWORD*>(base + offset);
	}

	void WriteDword(unsigned char* base, DWORD offset, DWORD value)
	{
		*reinterpret_cast<DWORD*>(base + offset) = value;
	}

	DWORD ComboCommandTier(DWORD combo)
	{
		if (combo >= 200) {
			return 200;
		}
		if (combo >= 100) {
			return 100;
		}
		if (combo >= 30) {
			return 30;
		}
		return 0;
	}

	DWORD ComboDigitCount(DWORD combo)
	{
		DWORD digits = 1;
		while (combo >= 10 && digits < kComboDigitLayerCount) {
			combo /= 10;
			digits++;
		}
		return digits;
	}

	void ClearComboDigitLayer(unsigned char* userLocal, DWORD index)
	{
		ClearLayerRef(userLocal + kComboDigitLayerBaseOffset + index * sizeof(DWORD), nullptr);
	}

	void ClearComboCommandLayers(unsigned char* userLocal)
	{
		ClearLayerRef(userLocal + kComboCommandLeftLayerOffset, nullptr);
		ClearLayerRef(userLocal + kComboCommandRightLayerOffset, nullptr);
	}

	void __fastcall BuildComboUiFromShowCombo(void* pThis, void* edx)
	{
		unsigned char* userLocal = reinterpret_cast<unsigned char*>(pThis);
		if (userLocal == nullptr) {
			return;
		}

		const DWORD combo = ReadDword(userLocal, kComboCountOffset);
		const DWORD digitCount = ComboDigitCount(combo);
		// The native builder does not immediately remove now-unused high digit layers when 105 -> 95 or 15 -> 5.
		// Clear only the excess layers so same-width combo changes still use the original animation path.
		for (DWORD i = digitCount; i < g_lastDigitCount && i < kComboDigitLayerCount; i++) {
			ClearComboDigitLayer(userLocal, i);
		}

		const DWORD tier = ComboCommandTier(combo);
		if (tier != g_lastCommandTier) {
			ClearComboCommandLayers(userLocal);

			if (tier > 0) {
				// Reuse the client's native 30/100/200 threshold path so the command hints match combo changes.
				WriteDword(userLocal, kComboCountOffset, tier);
				BuildComboUi(pThis, edx);
				WriteDword(userLocal, kComboCountOffset, combo);
			}

			g_lastCommandTier = tier;
		}

		BuildComboUi(pThis, edx);
		g_lastDigitCount = digitCount;
	}

	void __fastcall TimedClearComboUi(void* pThis, void* edx)
	{
		unsigned char* userLocal = reinterpret_cast<unsigned char*>(pThis);
		if (userLocal == nullptr) {
			return;
		}

		const DWORD combo = ReadDword(userLocal, kComboCountOffset);
		if (combo == 0) {
			return;
		}

		const DWORD now = ClientTick();
		const DWORD lastShownAt = ReadDword(userLocal, kComboLastShownAtOffset);
		const DWORD elapsed = now - lastShownAt;

		// The server keeps combo for 5s, then usually sends the first decay update near 8s.
		// Keep the combo number and command hints alive together until that update can refresh them.
		if (elapsed >= kComboUiTtlMs) {
			WriteDword(userLocal, kComboCommandVisibleOffset, 0);
			g_lastCommandTier = 0;
			g_lastDigitCount = 0;
			ClearComboUi(pThis, edx);
		}
	}
}

void AranComboUi::Install()
{
	Memory::WriteInt(kComboDigitLayerTtlPatch, kComboUiTtlMs);
	Memory::WriteInt(kComboTextLayerTtlPatch, kComboUiTtlMs);

	const DWORD showComboBuildCall = reinterpret_cast<DWORD>(&BuildComboUiFromShowCombo) - (kShowComboBuildCall + 5);
	Memory::WriteInt(kShowComboBuildCall + 1, showComboBuildCall);

	const DWORD relativeCall = reinterpret_cast<DWORD>(&TimedClearComboUi) - (kTimedClearCall + 5);
	Memory::WriteInt(kTimedClearCall + 1, relativeCall);
}
