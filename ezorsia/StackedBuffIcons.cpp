#include "stdafx.h"
#include "ClientLog.h"
#include <strsafe.h>
#include "StackedBuffIcons.h"
#include "Memory.h"

#include <algorithm>
#include <cstdarg>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	constexpr DWORD kOpcodeUpdateStackedBuffIcons = 0x1002;
	constexpr DWORD kOpcodeUpdateHurricaneFocus = 0x1009;
	constexpr int kBowExpert = 3120005;
	volatile LONG g_hurricaneFocusStacks = 0;
	constexpr WORD kStackedBuffIconPacketMagic = 0xBD1C;
	constexpr DWORD kTemporaryStatViewDrawAddr = 0x007B2BB0;
	constexpr DWORD kTemporaryStatViewAddIconAddr = 0x007B24D5;
	constexpr DWORD kNativeAddIconStringDedupAddr = 0x007B259C;
	constexpr DWORD kTemporaryStatViewRemoveNodeAddr = 0x007B4BD1;
	constexpr DWORD kWvsContextPtr = 0x00BE7918;
	constexpr DWORD kTemporaryStatViewContextOffset = 0x2EA8;
	constexpr DWORD kTemporaryStatViewVtable = 0x00B3F59C;
	constexpr DWORD kTemporaryStatViewListVtable = 0x00B3F5A0;
	constexpr DWORD kNativeSyncRetryInterval = 250;
	constexpr int kIconRecordSize = 17;
	constexpr int kMaxIcons = 64;
	constexpr int kNativeIconTypeSkill = 2;
	constexpr int kNativeIconTypeItem = 1;
	constexpr DWORD kTemporaryStatViewListCountOffset = 0x0C;
	constexpr DWORD kTemporaryStatViewListHeadOffset = 0x10;

	struct CInPacket
	{
		int Loopback;
		int State;
		void* Data;
		unsigned short DataLen;
		unsigned short RawSeq;
		unsigned int Unknown;
		unsigned int Offset;
	};
	static_assert(offsetof(CInPacket, DataLen) == 0x0C, "Unexpected CInPacket data length offset");
	static_assert(offsetof(CInPacket, Offset) == 0x14, "Unexpected CInPacket read offset");

	struct StackedBuffIcon
	{
		int sourceId = 0;
		int iconId = 0;
		bool skill = false;
		int leftDuration = 0;
		int duration = 0;
		DWORD receivedAt = 0;
	};

	CRITICAL_SECTION g_iconLock;
	bool g_iconLockInitialized = false;
	bool g_logEnabled = false;
	DWORD g_currentTemporaryStatView = 0;
	DWORD g_observedTemporaryStatView = 0;
	bool g_addIconHookInstalled = false;
	bool g_drawHookInstalled = false;
	bool g_callingVirtualAddIcon = false;
	bool g_syncingNativeIcons = false;
	bool g_nativeSyncPending = false;
	DWORD g_lastNativeSyncAttempt = 0;
	bool g_addIconStringDedupDisabled = false;
	DWORD g_lastNativeDrawView = 0;
	bool g_countdownFieldActive = false;
	std::vector<StackedBuffIcon> g_icons;
	std::unordered_map<DWORD, unsigned long long> g_virtualNativeNodes;
	BYTE g_addIconStringDedupOriginal[5] = { 0x6A, 0x00, 0x83, 0xC1, 0x0C };
	BYTE g_addIconStringDedupPatch[5] = { 0xE9, 0x6A, 0xFF, 0xFF, 0xFF };

	using NativeAddIconFunc = void(__fastcall*)(DWORD pThis, void* edx, int nativeType, int iconId, int duration,
		DWORD text0, DWORD text1, DWORD text2, DWORD text3, DWORD unknown);
	NativeAddIconFunc g_nativeAddIcon = reinterpret_cast<NativeAddIconFunc>(kTemporaryStatViewAddIconAddr);

	using NativeDrawFunc = void(__fastcall*)(DWORD pThis, void* edx);
	NativeDrawFunc g_nativeDraw = reinterpret_cast<NativeDrawFunc>(kTemporaryStatViewDrawAddr);

	void DebugLog(const char* format, ...);
	void SyncNativeVirtualIcons(const std::vector<StackedBuffIcon>& icons);
	bool RequestedCountsSatisfied(DWORD temporaryStatView, const std::vector<StackedBuffIcon>& icons);

	struct OverlayVertex
	{
		float x;
		float y;
		float z;
		float rhw;
		DWORD color;
	};

	bool WriteCodeBytes(DWORD address, const BYTE* bytes, int count)
	{
		if (!address || !bytes || count <= 0)
		{
			return false;
		}

		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<LPVOID>(address), count, PAGE_EXECUTE_READWRITE, &oldProtect))
		{
			return false;
		}
		for (int i = 0; i < count; i++)
		{
			reinterpret_cast<BYTE*>(address)[i] = bytes[i];
		}
		DWORD ignored = 0;
		VirtualProtect(reinterpret_cast<LPVOID>(address), count, oldProtect, &ignored);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), count);
		return true;
	}

	void SetNativeAddIconStringDedupDisabled(bool disabled)
	{
		if (g_addIconStringDedupDisabled == disabled)
		{
			return;
		}

		const BYTE* bytes = disabled ? g_addIconStringDedupPatch : g_addIconStringDedupOriginal;
		if (WriteCodeBytes(kNativeAddIconStringDedupAddr, bytes, sizeof(g_addIconStringDedupOriginal)))
		{
			g_addIconStringDedupDisabled = disabled;
		}
		else
		{
			DebugLog("native_add_dedup_patch_failed disabled=%d addr=%08X", disabled ? 1 : 0, kNativeAddIconStringDedupAddr);
		}
	}

	int NativeTypeForIcon(const StackedBuffIcon& icon)
	{
		return icon.skill ? kNativeIconTypeSkill : kNativeIconTypeItem;
	}

	bool IconExpired(const StackedBuffIcon& icon, DWORD now)
	{
		if (icon.leftDuration <= 0)
		{
			return false;
		}

		return now - icon.receivedAt >= static_cast<DWORD>(icon.leftDuration);
	}

	std::vector<StackedBuffIcon> SnapshotIcons()
	{
		std::vector<StackedBuffIcon> icons;
		if (!g_iconLockInitialized)
		{
			return icons;
		}

		const DWORD now = GetTickCount();
		EnterCriticalSection(&g_iconLock);
		g_icons.erase(
			std::remove_if(g_icons.begin(), g_icons.end(), [now](const StackedBuffIcon& icon) {
				return IconExpired(icon, now);
			}),
			g_icons.end());
		icons = g_icons;
		LeaveCriticalSection(&g_iconLock);
		if (InterlockedCompareExchange(&g_hurricaneFocusStacks, 0, 0) > 0) {
			StackedBuffIcon focus{};
			focus.sourceId = kBowExpert;
			focus.iconId = kBowExpert;
			focus.skill = true;
			focus.duration = 86400000;
			icons.push_back(focus);
		}
		return icons;
	}

	void __fastcall NativeAddIconHook(DWORD pThis, void* edx, int nativeType, int iconId, int duration,
		DWORD text0, DWORD text1, DWORD text2, DWORD text3, DWORD unknown)
	{
		const bool shouldObserve = pThis && !g_callingVirtualAddIcon && !g_syncingNativeIcons && g_iconLockInitialized;
		const DWORD previousTemporaryStatView = g_currentTemporaryStatView;
		if (shouldObserve)
		{
			g_observedTemporaryStatView = pThis;
			g_currentTemporaryStatView = pThis;
			DebugLog("native_add_hook observed=%08X type=%d icon=%d duration=%d", pThis, nativeType, iconId, duration);
		}

		g_nativeAddIcon(pThis, edx, nativeType, iconId, duration, text0, text1, text2, text3, unknown);

		if (shouldObserve && !g_syncingNativeIcons)
		{
			std::vector<StackedBuffIcon> icons = SnapshotIcons();
			// Regular buff packets can rebuild the native list; restore our requested counts when any tracked icon went missing.
			if (!RequestedCountsSatisfied(pThis, icons))
			{
				DebugLog("native_add_resync view=%08X type=%d icon=%d stored=%d",
					pThis, nativeType, iconId, static_cast<int>(icons.size()));
				SyncNativeVirtualIcons(icons);
			}
		}
	}

	void __fastcall NativeDrawHook(DWORD pThis, void* edx)
	{
		const DWORD previousTemporaryStatView = g_currentTemporaryStatView;
		if (pThis)
		{
			g_observedTemporaryStatView = pThis;
			g_currentTemporaryStatView = pThis;
		}

		g_nativeDraw(pThis, edx);
		if (pThis)
		{
			g_lastNativeDrawView = pThis;
		}

		if (pThis && !g_syncingNativeIcons && g_iconLockInitialized)
		{
			std::vector<StackedBuffIcon> icons = SnapshotIcons();
			if (!RequestedCountsSatisfied(pThis, icons))
			{
				DebugLog("native_draw_resync view=%08X previous=%08X stored=%d",
					pThis, previousTemporaryStatView, static_cast<int>(icons.size()));
				SyncNativeVirtualIcons(icons);
			}
		}
	}

	void DebugLog(const char* format, ...)
	{
		if (!g_logEnabled || !format)
		{
			return;
		}

		char message[512]{};
		va_list args;
		va_start(args, format);
		StringCchVPrintfA(message, ARRAYSIZE(message), format, args);
		va_end(args);

		ClientLog::Append(ClientLog::Component::BuffIcons, "%s", message);
	}

	unsigned short ReadUInt16LE(const unsigned char* data)
	{
		return static_cast<unsigned short>(data[0] | (data[1] << 8));
	}

	int ReadInt32LE(const unsigned char* data)
	{
		return static_cast<int>(
			static_cast<unsigned int>(data[0]) |
			(static_cast<unsigned int>(data[1]) << 8) |
			(static_cast<unsigned int>(data[2]) << 16) |
			(static_cast<unsigned int>(data[3]) << 24));
	}

	bool TryFindIconPacketLayout(const unsigned char* data, unsigned long size, int* countOffset, int* iconCount)
	{
		if (!data || !countOffset || !iconCount)
		{
			return false;
		}

		const int opcodeOffset = 4;
		const int magicOffset = opcodeOffset + 2;
		const int candidateCountOffset = opcodeOffset + 4;
		if (size < static_cast<unsigned long>(candidateCountOffset + 2))
		{
			return false;
		}

		if (ReadUInt16LE(data + opcodeOffset) != kOpcodeUpdateStackedBuffIcons)
		{
			return false;
		}

		if (ReadUInt16LE(data + magicOffset) != kStackedBuffIconPacketMagic)
		{
			return false;
		}

		const int count = ReadUInt16LE(data + candidateCountOffset);
		if (count > kMaxIcons)
		{
			return false;
		}

		const unsigned long requiredSize = static_cast<unsigned long>(candidateCountOffset + 2)
			+ static_cast<unsigned long>(count) * kIconRecordSize;
		if (size < requiredSize)
		{
			return false;
		}

		*countOffset = candidateCountOffset;
		*iconCount = count;
		return true;
	}

	bool TryReadDword(DWORD address, DWORD* value)
	{
		if (!address || !value)
		{
			return false;
		}

		__try
		{
			*value = *reinterpret_cast<DWORD*>(address);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			*value = 0;
			return false;
		}
	}

	DWORD ReadDwordOrZero(DWORD address)
	{
		DWORD value = 0;
		TryReadDword(address, &value);
		return value;
	}

	DWORD ResolveTemporaryStatView()
	{
		if (!g_countdownFieldActive)
		{
			return 0;
		}

		// CWvsContext owns this view across maps (native access at 0x0094D13C).
		// AddIcon/Draw are not called for an unchanged or empty buff bar.
		const DWORD context = ReadDwordOrZero(kWvsContextPtr);
		if (!context || context > MAXDWORD - kTemporaryStatViewContextOffset - 0x04)
		{
			return 0;
		}
		const DWORD view = context + kTemporaryStatViewContextOffset;
		// Constructor 0x00A01D9B initializes both vtables before the list is usable.
		if (ReadDwordOrZero(view) != kTemporaryStatViewVtable
			|| ReadDwordOrZero(view + 0x04) != kTemporaryStatViewListVtable)
		{
			return 0;
		}
		g_observedTemporaryStatView = view;
		g_currentTemporaryStatView = view;
		return g_currentTemporaryStatView;
	}

	DWORD GetNativeListHead(DWORD temporaryStatView)
	{
		if (!temporaryStatView)
		{
			return 0;
		}

		return ReadDwordOrZero(temporaryStatView + kTemporaryStatViewListHeadOffset);
	}

	DWORD GetNativeNodeNext(DWORD node)
	{
		if (!node)
		{
			return 0;
		}

		const DWORD rawNext = ReadDwordOrZero(node - 0x0C);
		return rawNext ? rawNext + 0x10 : 0;
	}

	DWORD GetNativeNodeEntry(DWORD node)
	{
		return ReadDwordOrZero(node + 0x04);
	}

	bool NativeEntryMatches(DWORD entry, int nativeType, int iconId)
	{
		if (!entry)
		{
			return false;
		}

		return ReadDwordOrZero(entry + 0x1C) == static_cast<DWORD>(nativeType)
			&& ReadDwordOrZero(entry + 0x20) == static_cast<DWORD>(iconId);
	}

	unsigned long long NativeIconKey(int nativeType, int iconId)
	{
		return (static_cast<unsigned long long>(static_cast<unsigned int>(nativeType)) << 32)
			| static_cast<unsigned int>(iconId);
	}

	unsigned long long NativeEntryKey(DWORD entry)
	{
		if (!entry)
		{
			return 0;
		}

		const int nativeType = static_cast<int>(ReadDwordOrZero(entry + 0x1C));
		const int iconId = static_cast<int>(ReadDwordOrZero(entry + 0x20));
		if (nativeType <= 0 || iconId <= 0)
		{
			return 0;
		}
		return NativeIconKey(nativeType, iconId);
	}

	bool NativeNodeIsVirtual(DWORD node)
	{
		if (!node)
		{
			return false;
		}

		auto marked = g_virtualNativeNodes.find(node);
		if (marked == g_virtualNativeNodes.end())
		{
			return false;
		}

		return NativeEntryKey(GetNativeNodeEntry(node)) == marked->second;
	}

	int EstimateNativeIconX(int index, int iconCount)
	{
		return Client::m_nGameWidth - 3 - (iconCount - index) * 32;
	}

	int EstimateNativeIconY(int index)
	{
		return 23;
	}

	int CountNativeNodes(DWORD temporaryStatView)
	{
		int count = 0;
		DWORD node = GetNativeListHead(temporaryStatView);
		for (int guard = 0; node && guard < 64; guard++)
		{
			count++;
			node = GetNativeNodeNext(node);
		}
		return count;
	}

	bool CountdownTextForRemaining(DWORD remainingMs, char* text, int textCapacity, COLORREF* color)
	{
		if (!text || textCapacity <= 0 || !color || remainingMs == 0 || remainingMs > 3600000)
		{
			return false;
		}

		int value = 0;
		if (remainingMs <= 60000)
		{
			value = static_cast<int>((remainingMs + 999) / 1000);
			*color = RGB(255, 222, 0);
		}
		else
		{
			value = static_cast<int>((remainingMs + 59999) / 60000);
			*color = RGB(93, 235, 95);
		}

		if (value < 1)
		{
			value = 1;
		}
		else if (value > 60)
		{
			value = 60;
		}

		wsprintfA(text, "%d", value);
		return true;
	}

	DWORD D3dColor(COLORREF color)
	{
		return 0xFF000000
			| (static_cast<DWORD>(GetRValue(color)) << 16)
			| (static_cast<DWORD>(GetGValue(color)) << 8)
			| static_cast<DWORD>(GetBValue(color));
	}

	void AddQuad(std::vector<OverlayVertex>& vertices, float x, float y, float width, float height, DWORD color)
	{
		if (width <= 0.0f || height <= 0.0f)
		{
			return;
		}

		const float left = x - 0.5f;
		const float top = y - 0.5f;
		const float right = x + width - 0.5f;
		const float bottom = y + height - 0.5f;
		const OverlayVertex v0{ left, top, 0.0f, 1.0f, color };
		const OverlayVertex v1{ right, top, 0.0f, 1.0f, color };
		const OverlayVertex v2{ right, bottom, 0.0f, 1.0f, color };
		const OverlayVertex v3{ left, bottom, 0.0f, 1.0f, color };
		vertices.push_back(v0);
		vertices.push_back(v1);
		vertices.push_back(v2);
		vertices.push_back(v0);
		vertices.push_back(v2);
		vertices.push_back(v3);
	}

	void AddDigitSegments(std::vector<OverlayVertex>& vertices, int digit, float x, float y, DWORD color)
	{
		static const unsigned char rows[10][5] = {
			{ 0x7, 0x5, 0x5, 0x5, 0x7 },
			{ 0x2, 0x6, 0x2, 0x2, 0x7 },
			{ 0x7, 0x1, 0x7, 0x4, 0x7 },
			{ 0x7, 0x1, 0x7, 0x1, 0x7 },
			{ 0x5, 0x5, 0x7, 0x1, 0x1 },
			{ 0x7, 0x4, 0x7, 0x1, 0x7 },
			{ 0x7, 0x4, 0x7, 0x5, 0x7 },
			{ 0x7, 0x1, 0x2, 0x2, 0x2 },
			{ 0x7, 0x5, 0x7, 0x5, 0x7 },
			{ 0x7, 0x5, 0x7, 0x1, 0x7 }
		};
		if (digit < 0 || digit > 9)
		{
			return;
		}

		const float pixel = 2.0f;
		for (int row = 0; row < 5; row++)
		{
			for (int column = 0; column < 3; column++)
			{
				if ((rows[digit][row] & (1 << (2 - column))) != 0)
				{
					AddQuad(vertices, x + column * pixel, y + row * pixel, pixel, pixel, color);
				}
			}
		}
	}

	void AddCountdownNumber(std::vector<OverlayVertex>& vertices, int value, int iconX, int iconY, COLORREF color)
	{
		char text[4]{};
		wsprintfA(text, "%d", value);
		const int length = lstrlenA(text);
		if (length <= 0)
		{
			return;
		}

		const float digitWidth = 6.0f;
		const float gap = 1.0f;
		const float textWidth = length * digitWidth + (length - 1) * gap;
		const float startX = static_cast<float>(iconX) + 32.0f - textWidth - 3.0f;
		const float startY = static_cast<float>(iconY) + 32.0f - 12.0f;
		const DWORD shadowColor = 0xD0000000;
		const DWORD textColor = D3dColor(color);

		for (int pass = 0; pass < 2; pass++)
		{
			const float offset = pass == 0 ? 1.0f : 0.0f;
			const DWORD passColor = pass == 0 ? shadowColor : textColor;
			for (int i = 0; i < length; i++)
			{
				AddDigitSegments(vertices, text[i] - '0',
					startX + i * (digitWidth + gap) + offset,
					startY + offset,
					passColor);
			}
		}
	}

	bool DrawOverlayVertices(void* d3dDevice, const std::vector<OverlayVertex>& vertices)
	{
		if (!d3dDevice || vertices.empty())
		{
			return true;
		}

		void** vtable = *reinterpret_cast<void***>(d3dDevice);
		if (!vtable)
		{
			return false;
		}

		typedef HRESULT(STDMETHODCALLTYPE* SetRenderStateFunc)(void*, DWORD, DWORD);
		typedef HRESULT(STDMETHODCALLTYPE* SetTextureFunc)(void*, DWORD, void*);
		typedef HRESULT(STDMETHODCALLTYPE* SetVertexShaderFunc)(void*, DWORD);
		typedef HRESULT(STDMETHODCALLTYPE* CreateStateBlockFunc)(void*, int, DWORD*);
		typedef HRESULT(STDMETHODCALLTYPE* ApplyStateBlockFunc)(void*, DWORD);
		typedef HRESULT(STDMETHODCALLTYPE* DeleteStateBlockFunc)(void*, DWORD);
		typedef HRESULT(STDMETHODCALLTYPE* SetTextureStageStateFunc)(void*, DWORD, DWORD, DWORD);
		typedef HRESULT(STDMETHODCALLTYPE* DrawPrimitiveUpFunc)(void*, int, UINT, const void*, UINT);
		SetRenderStateFunc setRenderState = reinterpret_cast<SetRenderStateFunc>(vtable[50]);
		SetTextureFunc setTexture = reinterpret_cast<SetTextureFunc>(vtable[61]);
		SetVertexShaderFunc setVertexShader = reinterpret_cast<SetVertexShaderFunc>(vtable[76]);
		CreateStateBlockFunc createStateBlock = reinterpret_cast<CreateStateBlockFunc>(vtable[57]);
		ApplyStateBlockFunc applyStateBlock = reinterpret_cast<ApplyStateBlockFunc>(vtable[54]);
		DeleteStateBlockFunc deleteStateBlock = reinterpret_cast<DeleteStateBlockFunc>(vtable[56]);
		SetTextureStageStateFunc setTextureStageState = reinterpret_cast<SetTextureStageStateFunc>(vtable[63]);
		DrawPrimitiveUpFunc drawPrimitiveUp = reinterpret_cast<DrawPrimitiveUpFunc>(vtable[72]);
		if (!setRenderState || !setTexture || !setVertexShader || !createStateBlock || !applyStateBlock || !deleteStateBlock
			|| !setTextureStageState || !drawPrimitiveUp)
		{
			return false;
		}

		DWORD stateBlock = 0;
		constexpr int kD3dStateBlockAll = 1;
		if (createStateBlock(d3dDevice, kD3dStateBlockAll, &stateBlock) < 0 || stateBlock == 0)
		{
			return false;
		}

		constexpr DWORD kD3dRsZEnable = 7;
		constexpr DWORD kD3dRsZWriteEnable = 14;
		constexpr DWORD kD3dRsSrcBlend = 19;
		constexpr DWORD kD3dRsDestBlend = 20;
		constexpr DWORD kD3dRsCullMode = 22;
		constexpr DWORD kD3dRsAlphaBlendEnable = 27;
		constexpr DWORD kD3dRsLighting = 137;
		constexpr DWORD kD3dBlendSrcAlpha = 5;
		constexpr DWORD kD3dBlendInvSrcAlpha = 6;
		constexpr DWORD kD3dCullNone = 1;
		constexpr DWORD kD3dFvfXyzRhwDiffuse = 0x004 | 0x040;
		constexpr DWORD kD3dTssColorOp = 1;
		constexpr DWORD kD3dTssColorArg1 = 2;
		constexpr DWORD kD3dTssAlphaOp = 4;
		constexpr DWORD kD3dTssAlphaArg1 = 5;
		constexpr DWORD kD3dTopSelectArg1 = 2;
		constexpr DWORD kD3dTaDiffuse = 0;
		constexpr int kD3dPtTriangleList = 4;

		setTexture(d3dDevice, 0, nullptr);
		setRenderState(d3dDevice, kD3dRsZEnable, 0);
		setRenderState(d3dDevice, kD3dRsZWriteEnable, 0);
		setRenderState(d3dDevice, kD3dRsAlphaBlendEnable, 1);
		setRenderState(d3dDevice, kD3dRsSrcBlend, kD3dBlendSrcAlpha);
		setRenderState(d3dDevice, kD3dRsDestBlend, kD3dBlendInvSrcAlpha);
		setRenderState(d3dDevice, kD3dRsCullMode, kD3dCullNone);
		setRenderState(d3dDevice, kD3dRsLighting, 0);
		setTextureStageState(d3dDevice, 0, kD3dTssColorOp, kD3dTopSelectArg1);
		setTextureStageState(d3dDevice, 0, kD3dTssColorArg1, kD3dTaDiffuse);
		setTextureStageState(d3dDevice, 0, kD3dTssAlphaOp, kD3dTopSelectArg1);
		setTextureStageState(d3dDevice, 0, kD3dTssAlphaArg1, kD3dTaDiffuse);

		const bool drawn = setVertexShader(d3dDevice, kD3dFvfXyzRhwDiffuse) >= 0
			&& drawPrimitiveUp(d3dDevice, kD3dPtTriangleList,
				static_cast<UINT>(vertices.size() / 3), vertices.data(), sizeof(OverlayVertex)) >= 0;
		applyStateBlock(d3dDevice, stateBlock);
		deleteStateBlock(d3dDevice, stateBlock);
		return drawn;
	}

	std::unordered_map<unsigned long long, int> CountNativeIconsByKey(DWORD temporaryStatView)
	{
		std::unordered_map<unsigned long long, int> counts;
		DWORD node = GetNativeListHead(temporaryStatView);
		for (int guard = 0; node && guard < 256; guard++)
		{
			const unsigned long long key = NativeEntryKey(GetNativeNodeEntry(node));
			if (key != 0)
			{
				counts[key]++;
			}
			node = GetNativeNodeNext(node);
		}
		return counts;
	}

	std::unordered_map<unsigned long long, int> DesiredIconCounts(const std::vector<StackedBuffIcon>& icons)
	{
		std::unordered_map<unsigned long long, int> counts;
		for (const StackedBuffIcon& icon : icons)
		{
			if (icon.iconId > 0)
			{
				counts[NativeIconKey(NativeTypeForIcon(icon), icon.iconId)]++;
			}
		}
		return counts;
	}

	bool RequestedCountsSatisfied(DWORD temporaryStatView, const std::vector<StackedBuffIcon>& icons)
	{
		if (!temporaryStatView || icons.empty())
		{
			return true;
		}

		std::unordered_map<unsigned long long, int> nativeCounts = CountNativeIconsByKey(temporaryStatView);
		for (const auto& desired : DesiredIconCounts(icons))
		{
			auto nativeCount = nativeCounts.find(desired.first);
			if (nativeCount == nativeCounts.end() || nativeCount->second < desired.second)
			{
				return false;
			}
		}
		return true;
	}

	std::unordered_set<DWORD> SnapshotNativeIconNodes(DWORD temporaryStatView, int nativeType, int iconId)
	{
		std::unordered_set<DWORD> nodes;
		DWORD node = GetNativeListHead(temporaryStatView);
		for (int guard = 0; node && guard < 256; guard++)
		{
			const DWORD entry = GetNativeNodeEntry(node);
			if (NativeEntryMatches(entry, nativeType, iconId))
			{
				nodes.insert(node);
			}
			node = GetNativeNodeNext(node);
		}
		return nodes;
	}

	int MarkNewNativeIcon(DWORD temporaryStatView, int nativeType, int iconId, const std::unordered_set<DWORD>& before)
	{
		int marked = 0;
		DWORD node = GetNativeListHead(temporaryStatView);
		for (int guard = 0; node && guard < 256; guard++)
		{
			const DWORD entry = GetNativeNodeEntry(node);
			if (NativeEntryMatches(entry, nativeType, iconId)
				&& before.find(node) == before.end()
				&& !NativeNodeIsVirtual(node))
			{
				g_virtualNativeNodes[node] = NativeIconKey(nativeType, iconId);
				marked++;
			}
			node = GetNativeNodeNext(node);
		}
		return marked;
	}

	bool TryCallNativeAddIcon(DWORD temporaryStatView, int nativeType, int iconId, int duration)
	{
		bool succeeded = false;
		SetNativeAddIconStringDedupDisabled(true);
		__try
		{
			g_callingVirtualAddIcon = true;
			__asm
			{
				push 0
				sub esp, 10h
				mov dword ptr [esp], 0
				mov dword ptr [esp + 4], 0
				mov dword ptr [esp + 8], 0
				mov dword ptr [esp + 0Ch], 0
				push duration
				push iconId
				push nativeType
				mov ecx, temporaryStatView
				mov eax, kTemporaryStatViewAddIconAddr
				call eax
			}
			g_callingVirtualAddIcon = false;
			succeeded = true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			g_callingVirtualAddIcon = false;
			succeeded = false;
		}
		SetNativeAddIconStringDedupDisabled(false);
		return succeeded;
	}

	bool TryRemoveNativeNode(DWORD temporaryStatView, DWORD node)
	{
		typedef void(__thiscall* RemoveNodeFunc)(DWORD pThis, DWORD node);
		__try
		{
			reinterpret_cast<RemoveNodeFunc>(kTemporaryStatViewRemoveNodeAddr)(temporaryStatView + 0x04, node);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	int PruneStackedNativeIcons(DWORD temporaryStatView, const std::unordered_map<unsigned long long, int>& desiredCounts)
	{
		if (!temporaryStatView)
		{
			g_virtualNativeNodes.clear();
			return 0;
		}

		int removed = 0;
		std::unordered_map<unsigned long long, int> seenCounts;
		std::unordered_map<DWORD, unsigned long long> activeVirtualNodes;
		DWORD node = GetNativeListHead(temporaryStatView);
		for (int guard = 0; node && guard < 256; guard++)
		{
			const DWORD next = GetNativeNodeNext(node);
			const DWORD entry = GetNativeNodeEntry(node);
			const unsigned long long key = NativeEntryKey(entry);
			const bool isVirtual = NativeNodeIsVirtual(node);
			bool shouldRemove = false;

			auto desiredCount = desiredCounts.find(key);
			if (key != 0 && desiredCount != desiredCounts.end())
			{
				seenCounts[key]++;
				shouldRemove = seenCounts[key] > desiredCount->second;
			}
			else
			{
				shouldRemove = isVirtual;
			}

			if (shouldRemove)
			{
				if (TryRemoveNativeNode(temporaryStatView, node))
				{
					removed++;
				}
				else
				{
					DebugLog("native_prune_exception view=%08X node=%08X entry=%08X key=%08X%08X",
						temporaryStatView, node, entry, static_cast<DWORD>(key >> 32), static_cast<DWORD>(key));
					if (isVirtual && key != 0)
					{
						// Keep ownership on failed removal so a later sync can retry instead of leaving a ghost native icon.
						activeVirtualNodes[node] = key;
					}
				}
			}
			else if (isVirtual && key != 0)
			{
				activeVirtualNodes[node] = key;
			}

			node = next;
		}

		g_virtualNativeNodes.swap(activeVirtualNodes);
		return removed;
	}

	bool AddNativeVirtualIcon(DWORD temporaryStatView, const StackedBuffIcon& icon)
	{
		if (!temporaryStatView || icon.iconId <= 0)
		{
			return false;
		}

		const int nativeType = NativeTypeForIcon(icon);
		const std::unordered_set<DWORD> before = SnapshotNativeIconNodes(temporaryStatView, nativeType, icon.iconId);
		const int duration = icon.leftDuration > 0 ? icon.leftDuration : icon.duration;

		DebugLog("native_add_try view=%08X source=%d type=%d icon=%d duration=%d before=%d",
			temporaryStatView, icon.sourceId, nativeType, icon.iconId, duration, static_cast<int>(before.size()));
		// Native AddIcon collapses duplicate icon strings, so temporarily bypass that check for synthetic entries.
		if (!TryCallNativeAddIcon(temporaryStatView, nativeType, icon.iconId, duration))
		{
			DebugLog("native_add_exception view=%08X source=%d type=%d icon=%d", temporaryStatView, icon.sourceId, nativeType, icon.iconId);
			return false;
		}

		const int marked = MarkNewNativeIcon(temporaryStatView, nativeType, icon.iconId, before);
		DebugLog("native_add_result view=%08X source=%d type=%d icon=%d marked=%d",
			temporaryStatView, icon.sourceId, nativeType, icon.iconId, marked);
		return marked > 0;
	}

	void DrawNativeTemporaryStatView(DWORD temporaryStatView)
	{
		if (!temporaryStatView)
		{
			return;
		}

		typedef void(__thiscall* DrawFunc)(DWORD pThis);
		__try
		{
			reinterpret_cast<DrawFunc>(kTemporaryStatViewDrawAddr)(temporaryStatView);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			DebugLog("native_draw_exception view=%08X", temporaryStatView);
		}
	}

	int ReadNativeIconCount(void* temporaryStatView)
	{
		if (!temporaryStatView)
		{
			return 0;
		}

		DWORD rawCount = 0;
		if (!TryReadDword(reinterpret_cast<DWORD>(temporaryStatView) + kTemporaryStatViewListCountOffset, &rawCount))
		{
			return 0;
		}

		if (rawCount > 64)
		{
			return 0;
		}
		return static_cast<int>(rawCount);
	}

	void SyncNativeVirtualIcons(const std::vector<StackedBuffIcon>& icons)
	{
		const DWORD temporaryStatView = ResolveTemporaryStatView();
		if (!temporaryStatView || temporaryStatView != g_observedTemporaryStatView)
		{
			g_nativeSyncPending = true;
			DebugLog("native_sync_deferred no_ready_view count=%d", static_cast<int>(icons.size()));
			return;
		}
		if (g_syncingNativeIcons)
		{
			g_nativeSyncPending = true;
			DebugLog("native_sync_skip busy view=%08X count=%d", temporaryStatView, static_cast<int>(icons.size()));
			return;
		}

		g_syncingNativeIcons = true;
		g_nativeSyncPending = false;
		g_lastNativeSyncAttempt = GetTickCount();
		const std::unordered_map<unsigned long long, int> desiredCounts = DesiredIconCounts(icons);
		const int removed = PruneStackedNativeIcons(temporaryStatView, desiredCounts);
		std::unordered_map<unsigned long long, int> nativeCounts = CountNativeIconsByKey(temporaryStatView);
		int coveredByNative = 0;
		int added = 0;
		for (const StackedBuffIcon& icon : icons)
		{
			if (icon.iconId <= 0)
			{
				continue;
			}

			const unsigned long long key = NativeIconKey(NativeTypeForIcon(icon), icon.iconId);
			auto nativeCount = nativeCounts.find(key);
			if (nativeCount != nativeCounts.end() && nativeCount->second > 0)
			{
				nativeCount->second--;
				coveredByNative++;
			}
			else if (AddNativeVirtualIcon(temporaryStatView, icon))
			{
				added++;
			}
			else
			{
				g_nativeSyncPending = true;
			}
		}

		if (removed > 0 || added > 0)
		{
			DrawNativeTemporaryStatView(temporaryStatView);
		}
		DebugLog("native_sync view=%08X removed=%d coveredNative=%d added=%d requested=%d nativeCount=%d",
			temporaryStatView, removed, coveredByNative, added, static_cast<int>(icons.size()), ReadNativeIconCount(reinterpret_cast<void*>(temporaryStatView)));
		g_syncingNativeIcons = false;
	}

	void ReplaceIcons(const std::vector<StackedBuffIcon>& icons)
	{
		if (!g_iconLockInitialized)
		{
			return;
		}

		EnterCriticalSection(&g_iconLock);
		g_icons = icons;
		LeaveCriticalSection(&g_iconLock);
		DebugLog("replace_icons count=%d view=%08X", static_cast<int>(icons.size()), ResolveTemporaryStatView());
		for (const StackedBuffIcon& icon : icons)
		{
			DebugLog("icon source=%d icon=%d skill=%d left=%d duration=%d",
				icon.sourceId, icon.iconId, icon.skill ? 1 : 0, icon.leftDuration, icon.duration);
		}
		SyncNativeVirtualIcons(SnapshotIcons());
	}

	bool TryParseIconPacket(CInPacket* packet, StackedBuffIcon* parsedIcons, int* parsedCount, bool* isTargetPacket)
	{
		if (parsedCount)
		{
			*parsedCount = 0;
		}
		if (isTargetPacket)
		{
			*isTargetPacket = false;
		}
		if (packet == nullptr || parsedIcons == nullptr || parsedCount == nullptr || isTargetPacket == nullptr)
		{
			return false;
		}

		__try
		{
			if (packet->Data == nullptr || packet->DataLen < 2)
			{
				return false;
			}

			const unsigned char* data = reinterpret_cast<const unsigned char*>(packet->Data);
			int countOffset = 0;
			int count = 0;
			if (!TryFindIconPacketLayout(data, packet->DataLen, &countOffset, &count))
			{
				return true;
			}

			*isTargetPacket = true;
			DebugLog("packet target size=%lu countOffset=%d count=%d offset=%u dataLen=%u rawSeq=%u",
				static_cast<unsigned long>(packet->DataLen), countOffset, count, packet->Offset, packet->DataLen, packet->RawSeq);

			const DWORD now = GetTickCount();
			const unsigned char* cursor = data + countOffset + 2;
			int accepted = 0;
			for (int i = 0; i < count; i++)
			{
				StackedBuffIcon icon{};
				icon.sourceId = ReadInt32LE(cursor);
				icon.iconId = ReadInt32LE(cursor + 4);
				icon.skill = cursor[8] != 0;
				icon.leftDuration = ReadInt32LE(cursor + 9);
				icon.duration = ReadInt32LE(cursor + 13);
				icon.receivedAt = now;
				cursor += kIconRecordSize;

				if (icon.iconId > 0)
				{
					parsedIcons[accepted++] = icon;
				}
			}

			*parsedCount = accepted;
			DebugLog("packet parsed accepted=%d", accepted);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			*isTargetPacket = true;
			*parsedCount = 0;
			DebugLog("packet parse exception");
			return false;
		}
	}
}

namespace StackedBuffIcons
{
	void Install(bool enableLog)
	{
		g_logEnabled = enableLog;
		if (!g_iconLockInitialized)
		{
			InitializeCriticalSection(&g_iconLock);
			g_iconLockInitialized = true;
		}

		DebugLog("install nativeAdd=%08X nativeDraw=%08X dedupPatch=%08X contextResolver=1 log=%d",
			kTemporaryStatViewAddIconAddr, kTemporaryStatViewDrawAddr, kNativeAddIconStringDedupAddr, g_logEnabled ? 1 : 0);
		if (!g_addIconHookInstalled)
		{
			g_addIconHookInstalled = Memory::SetHook(true, reinterpret_cast<void**>(&g_nativeAddIcon), NativeAddIconHook);
			DebugLog("install_add_hook result=%d target=%08X", g_addIconHookInstalled ? 1 : 0,
				reinterpret_cast<DWORD>(g_nativeAddIcon));
		}
		if (!g_drawHookInstalled)
		{
			g_drawHookInstalled = Memory::SetHook(true, reinterpret_cast<void**>(&g_nativeDraw), NativeDrawHook);
			DebugLog("install_draw_hook result=%d target=%08X", g_drawHookInstalled ? 1 : 0,
				reinterpret_cast<DWORD>(g_nativeDraw));
		}
	}

	bool HandlePacket(void* rawPacket)
	{
		CInPacket* packet = reinterpret_cast<CInPacket*>(rawPacket);
		if (packet && packet->Data && packet->DataLen >= 6) {
			const unsigned char* data = reinterpret_cast<const unsigned char*>(packet->Data);
			if ((data[4] | (data[5] << 8)) == kOpcodeUpdateHurricaneFocus) {
				if (packet->DataLen == 7 && data[6] <= 5) {
					InterlockedExchange(&g_hurricaneFocusStacks, data[6]);
					SyncNativeVirtualIcons(SnapshotIcons());
				}
				return true;
			}
		}
		StackedBuffIcon parsedIcons[kMaxIcons]{};
		int parsedCount = 0;
		bool isTargetPacket = false;
		const bool parsed = TryParseIconPacket(packet, parsedIcons, &parsedCount, &isTargetPacket);
		if (!isTargetPacket)
		{
			return false;
		}
		if (!parsed)
		{
			ReplaceIcons(std::vector<StackedBuffIcon>());
			DebugLog("packet consumed parse_failed");
			return true;
		}

		std::vector<StackedBuffIcon> icons;
		icons.reserve(parsedCount);
		for (int i = 0; i < parsedCount; i++)
		{
			icons.push_back(parsedIcons[i]);
		}

		ReplaceIcons(icons);
		DebugLog("packet consumed parsed=%d", parsedCount);
		return true;
	}

	void DrawCountdownOverlay(void* d3dDevice)
	{
		if (!d3dDevice || !g_countdownFieldActive || !g_lastNativeDrawView)
		{
			return;
		}

		const DWORD temporaryStatView = g_lastNativeDrawView;
		g_currentTemporaryStatView = temporaryStatView;
		const int nodeCount = CountNativeNodes(temporaryStatView);
		if (nodeCount <= 0)
		{
			return;
		}

		if (Client::m_nGameWidth <= 0 || Client::m_nGameHeight <= 0)
		{
			return;
		}

		std::vector<OverlayVertex> vertices;
		vertices.reserve(nodeCount * 2 * 2 * 7 * 6);
		DWORD node = GetNativeListHead(temporaryStatView);
		for (int index = 0; node && index < nodeCount && index < 64; index++)
		{
			const DWORD entry = GetNativeNodeEntry(node);
			char text[4]{};
			COLORREF color = RGB(255, 255, 255);
			const int focusStacks = InterlockedCompareExchange(&g_hurricaneFocusStacks, 0, 0);
			if (focusStacks > 0 && NativeEntryMatches(entry, kNativeIconTypeSkill, kBowExpert)) {
				AddCountdownNumber(vertices, focusStacks, EstimateNativeIconX(index, nodeCount),
					EstimateNativeIconY(index), RGB(255, 225, 80));
			}
			else if (CountdownTextForRemaining(ReadDwordOrZero(entry + 0x38), text, sizeof(text), &color))
			{
				const int iconX = EstimateNativeIconX(index, nodeCount);
				const int iconY = EstimateNativeIconY(index);
				AddCountdownNumber(vertices, atoi(text), iconX, iconY, color);
			}
			node = GetNativeNodeNext(node);
		}

		DrawOverlayVertices(d3dDevice, vertices);
	}

	void OnFieldUpdate()
	{
		const bool activating = !g_countdownFieldActive;
		if (activating)
		{
			DebugLog("field_update activate countdownView=%08X", g_lastNativeDrawView);
			g_nativeSyncPending = true;
		}
		g_countdownFieldActive = true;
		if (g_nativeSyncPending && g_iconLockInitialized
			&& (activating || GetTickCount() - g_lastNativeSyncAttempt >= kNativeSyncRetryInterval))
		{
			const DWORD view = ResolveTemporaryStatView();
			if (view)
			{
				g_lastNativeDrawView = view;
				SyncNativeVirtualIcons(SnapshotIcons());
			}
		}
	}

	void OnFieldInit()
	{
		InterlockedExchange(&g_hurricaneFocusStacks, 0);
		g_countdownFieldActive = false;
		g_currentTemporaryStatView = 0;
		g_observedTemporaryStatView = 0;
		g_lastNativeDrawView = 0;
		g_nativeSyncPending = true;
		// Keep ownership until the next field update prunes the persistent native list.
		DebugLog("field_init countdownView=%08X", g_lastNativeDrawView);
	}

	void OnFieldDispose()
	{
		InterlockedExchange(&g_hurricaneFocusStacks, 0);
		g_countdownFieldActive = false;
		g_currentTemporaryStatView = 0;
		g_observedTemporaryStatView = 0;
		g_lastNativeDrawView = 0;
		g_nativeSyncPending = true;
		DebugLog("field_dispose countdownView=%08X", g_lastNativeDrawView);
	}
}
