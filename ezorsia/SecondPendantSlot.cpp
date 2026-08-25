#include "stdafx.h"
#include "SecondPendantSlot.h"
#include "SecondPendantRules.h"
#include "detours.h"

#include <array>
#include <cstdarg>

namespace
{
struct SlotPoint
{
	int x;
	int y;
};

constexpr DWORD kWvsContextSingleton = 0x00BE7918;
constexpr DWORD kExtraPendantFlagOffset = 0x0000387C;
constexpr DWORD kCuiEquipExtraFlagOffset = 0x000005E8;

constexpr DWORD kCuiEquipConstructorAddress = 0x007FDE7C;
constexpr DWORD kCuiEquipExtraFlagStoreAddress = 0x007FDEFB;
constexpr DWORD kCuiEquipMouseButtonAddress = 0x007FE4C6;
constexpr DWORD kCuiEquipHitTestAddress = 0x007FEC32;
constexpr DWORD kCuiEquipDrawInitialLimitImmediate = 0x007FEE56;
constexpr DWORD kCuiEquipSlot51BlockedFillImmediate = 0x007FEEB8;
constexpr DWORD kCuiEquipDrawLoopLimitImmediate = 0x007FEFBE;
constexpr DWORD kSecondPendantExpirationRejectJump = 0x004F1CCA;

constexpr DWORD kNativeHitCoordinateTable = 0x00BE2260;
constexpr DWORD kNativeNormalCoordinateTable = 0x00BE23F0;
constexpr DWORD kNativeSecondPendantCoordinate =
	kNativeNormalCoordinateTable + (51 - 1) * sizeof(SlotPoint);

constexpr DWORD kPetPanelInitialAnchor = 0x007FE142;
constexpr DWORD kPetPanelMoveAnchor = 0x007FEC1A;
constexpr DWORD kPetPanelShowAnchor = 0x007FFD46;

constexpr int kNativePetPanelOffset = 0xAC;
constexpr int kSlotSize = 32;
constexpr int kNativeMaxEquipmentSlot = 50;
constexpr int kSecondPendantSlot = SecondPendantRules::kSecondPendantSlot;

constexpr SlotPoint kSecondPendantPosition{ 38, 101 };

constexpr std::array<SlotPoint, 52> kNormalSlotPositions = {
	SlotPoint{ 0, 0 },
	SlotPoint{ 38, 35 },  // 1  hat
	SlotPoint{ 38, 68 },  // 2  face accessory
	SlotPoint{ 71, 101 }, // 3  eye accessory
	SlotPoint{ 104, 101 },// 4  earrings
	SlotPoint{ 38, 134 }, // 5  top
	SlotPoint{ 38, 167 }, // 6  pants
	SlotPoint{ 71, 200 }, // 7  shoes
	SlotPoint{ 5, 167 },  // 8  gloves
	SlotPoint{ 5, 134 },  // 9  cape
	SlotPoint{ 137, 134 },// 10 shield
	SlotPoint{ 104, 134 },// 11 weapon
	SlotPoint{ 104, 167 },// 12 ring 1
	SlotPoint{ 137, 167 },// 13 ring 2
	SlotPoint{ 0, 0 },    // 14 pet equipment, handled by the pet panel
	SlotPoint{ 104, 68 }, // 15 ring 3
	SlotPoint{ 137, 68 }, // 16 ring 4
	SlotPoint{ 71, 134 }, // 17 pendant 1
	SlotPoint{ 5, 233 },  // 18 tamed mob
	SlotPoint{ 38, 233 }, // 19 saddle
	SlotPoint{ 71, 233 }, // 20 reserved native equipment slot
	SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 },
	SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 },
	SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 },
	SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 },
	SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 },
	SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 },
	SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 }, SlotPoint{ 0, 0 },
	SlotPoint{ 5, 68 },   // 49 medal
	SlotPoint{ 71, 167 }, // 50 belt
	kSecondPendantPosition
};

constexpr bool IsMainEquipmentSlot(int slot)
{
	return (slot >= 1 && slot <= 13)
		|| (slot >= 15 && slot <= 20)
		|| (slot >= 49 && slot <= 51);
}

constexpr bool NormalSlotsDoNotOverlap()
{
	for (int left = 1; left <= kSecondPendantSlot; ++left)
	{
		if (!IsMainEquipmentSlot(left))
		{
			continue;
		}
		for (int right = left + 1; right <= kSecondPendantSlot; ++right)
		{
			if (!IsMainEquipmentSlot(right))
			{
				continue;
			}
			if (kNormalSlotPositions[left].x == kNormalSlotPositions[right].x
				&& kNormalSlotPositions[left].y == kNormalSlotPositions[right].y)
			{
				return false;
			}
		}
	}
	return true;
}

static_assert(kNativePetPanelOffset == 172, "the pet panel must retain its original anchor");
static_assert(kNormalSlotPositions[11].x == 104 && kNormalSlotPositions[11].y == 134,
	"the weapon must retain its original position");
static_assert(kNormalSlotPositions[49].x == 5 && kNormalSlotPositions[49].y == 68,
	"the medal must retain its original position");
static_assert(kNormalSlotPositions[50].x == 71 && kNormalSlotPositions[50].y == 167,
	"the belt must retain its original position");
static_assert(kSecondPendantPosition.x == kNormalSlotPositions[2].x
	&& kSecondPendantPosition.y == kNormalSlotPositions[2].y + 33,
	"the second pendant must be exactly one row below the face accessory");
static_assert(NormalSlotsDoNotOverlap(), "the second pendant must not overlap any native equipment slot");

bool g_enableLog = false;
WCHAR g_logPath[MAX_PATH]{};
SRWLOCK g_logLock = SRWLOCK_INIT;
volatile LONG g_lastHoverSlot = -1;

using CuiEquipConstructor = void* (__fastcall*)(void* pThis, void* edx);
using CuiEquipHitTest = int(__fastcall*)(void* pThis, void* edx, int x, int y);
using CuiEquipMouseButton = void(__fastcall*)(void* pThis, void* edx, unsigned int message,
	unsigned int wParam, int x, int y);

CuiEquipConstructor g_cuiEquipConstructor = reinterpret_cast<CuiEquipConstructor>(kCuiEquipConstructorAddress);
CuiEquipHitTest g_cuiEquipHitTest = reinterpret_cast<CuiEquipHitTest>(kCuiEquipHitTestAddress);
CuiEquipMouseButton g_cuiEquipMouseButton = reinterpret_cast<CuiEquipMouseButton>(kCuiEquipMouseButtonAddress);

void InitializeLogPath()
{
	WCHAR exePath[MAX_PATH]{};
	if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
	{
		lstrcpynW(g_logPath, L"equipment_slot.log", MAX_PATH);
		return;
	}

	WCHAR* slash = wcsrchr(exePath, L'\\');
	if (slash == nullptr)
	{
		lstrcpynW(g_logPath, L"equipment_slot.log", MAX_PATH);
		return;
	}

	*(slash + 1) = L'\0';
	lstrcpynW(g_logPath, exePath, MAX_PATH);
	lstrcpynW(g_logPath + lstrlenW(g_logPath), L"equipment_slot.log",
		MAX_PATH - lstrlenW(g_logPath));
}

void WriteEquipmentLog(const char* format, ...)
{
	if (!g_enableLog)
	{
		return;
	}

	char message[768]{};
	va_list args;
	va_start(args, format);
	_vsnprintf_s(message, _countof(message), _TRUNCATE, format, args);
	va_end(args);

	SYSTEMTIME time{};
	GetLocalTime(&time);
	char line[896]{};
	_snprintf_s(line, _countof(line), _TRUNCATE,
		"%04u-%02u-%02u %02u:%02u:%02u.%03u [EquipmentSlot] %s\r\n",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
		time.wSecond, time.wMilliseconds, message);

	AcquireSRWLockExclusive(&g_logLock);
	HANDLE file = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE)
	{
		DWORD written = 0;
		WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
		CloseHandle(file);
	}
	ReleaseSRWLockExclusive(&g_logLock);
}

bool IsExtraPendantEnabled()
{
	__try
	{
		const DWORD context = *reinterpret_cast<const DWORD*>(kWvsContextSingleton);
		return context != 0 && *reinterpret_cast<const DWORD*>(context + kExtraPendantFlagOffset) != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

bool IsInsideSecondPendant(int x, int y)
{
	return x > kSecondPendantPosition.x && x < kSecondPendantPosition.x + kSlotSize
		&& y > kSecondPendantPosition.y && y < kSecondPendantPosition.y + kSlotSize;
}

int ResolveEquipmentSlot(void* pThis, void* edx, int x, int y)
{
	if (IsExtraPendantEnabled() && IsInsideSecondPendant(x, y))
	{
		return kSecondPendantSlot;
	}
	return g_cuiEquipHitTest(pThis, edx, x, y);
}

void* __fastcall CuiEquipConstructorHook(void* pThis, void* edx)
{
	const bool extraEnabled = IsExtraPendantEnabled();
	if (pThis != nullptr)
	{
		// Keep the native equipment window in its original, non-expanded mode.
		*reinterpret_cast<DWORD*>(reinterpret_cast<BYTE*>(pThis) + kCuiEquipExtraFlagOffset) = 0;
	}

	void* result = g_cuiEquipConstructor(pThis, edx);
	if (pThis != nullptr)
	{
		*reinterpret_cast<DWORD*>(reinterpret_cast<BYTE*>(pThis) + kCuiEquipExtraFlagOffset) = 0;
	}
	WriteEquipmentLog("constructor this=%08X serverExtra=%d uiExpanded=0",
		reinterpret_cast<DWORD>(pThis), extraEnabled ? 1 : 0);
	return result;
}

int __fastcall CuiEquipHitTestHook(void* pThis, void* edx, int x, int y)
{
	const bool extraEnabled = IsExtraPendantEnabled();
	const int slot = ResolveEquipmentSlot(pThis, edx, x, y);
	const LONG previous = InterlockedExchange(&g_lastHoverSlot, slot);
	if (previous != slot)
	{
		WriteEquipmentLog("hover serverExtra=%d uiExpanded=0 slot=%d x=%d y=%d",
			extraEnabled ? 1 : 0, slot, x, y);
	}
	return slot;
}

void __fastcall CuiEquipMouseButtonHook(void* pThis, void* edx, unsigned int message,
	unsigned int wParam, int x, int y)
{
	const int slot = ResolveEquipmentSlot(pThis, edx, x, y);
	WriteEquipmentLog("mouse message=0x%04X serverExtra=%d uiExpanded=0 slot=%d x=%d y=%d wParam=%u",
		message, IsExtraPendantEnabled() ? 1 : 0, slot, x, y, wParam);
	g_cuiEquipMouseButton(pThis, edx, message, wParam, x, y);
}

bool BytesMatch(DWORD address, const unsigned char* expected, size_t size)
{
	__try
	{
		return memcmp(reinterpret_cast<const void*>(address), expected, size) == 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

bool PointMatches(DWORD address, SlotPoint expected)
{
	__try
	{
		const SlotPoint actual = *reinterpret_cast<const SlotPoint*>(address);
		return actual.x == expected.x && actual.y == expected.y;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

bool NativeCoordinatesMatch()
{
	for (int slot = 1; slot <= kNativeMaxEquipmentSlot; ++slot)
	{
		if (!IsMainEquipmentSlot(slot))
		{
			continue;
		}

		const SlotPoint expected = kNormalSlotPositions[slot];
		const DWORD normalAddress = kNativeNormalCoordinateTable + (slot - 1) * sizeof(SlotPoint);
		const DWORD hitAddress = kNativeHitCoordinateTable + (slot - 1) * sizeof(SlotPoint);
		if (!PointMatches(normalAddress, expected) || !PointMatches(hitAddress, expected))
		{
			return false;
		}
	}
	return true;
}

bool ValidateNativeLayout()
{
	const unsigned char constructorBytes[] = { 0xB8, 0xAD, 0xF5, 0xAB, 0x00 };
	const unsigned char extraFlagStoreBytes[] = { 0x89, 0x86, 0xE8, 0x05, 0x00, 0x00 };
	const unsigned char mouseButtonBytes[] = { 0xB8, 0xAD, 0xF7, 0xAB, 0x00 };
	const unsigned char hitTestBytes[] = { 0x33, 0xD2, 0xB9, 0x64, 0x22, 0xBE, 0x00 };
	const unsigned char drawInitialLimitBytes[] = { 0x83, 0xC1, 0x32 };
	const unsigned char slot51BlockedFillBytes[] = { 0xC7, 0x45, 0xE8, 0x01, 0x00, 0x00, 0x00 };
	const unsigned char drawLoopLimitBytes[] = { 0x83, 0xC0, 0x32 };
	const unsigned char expirationRejectJumpBytes[] = { 0x0F, 0x8C, 0xA3, 0x0E, 0x00, 0x00 };
	const unsigned char petAnchorBytes[] = { 0x05, 0xAC, 0x00, 0x00, 0x00 };
	const SlotPoint nativeSlot51Alias{ 38, 35 };

	return BytesMatch(kCuiEquipConstructorAddress, constructorBytes, sizeof(constructorBytes))
		&& BytesMatch(kCuiEquipExtraFlagStoreAddress, extraFlagStoreBytes, sizeof(extraFlagStoreBytes))
		&& BytesMatch(kCuiEquipMouseButtonAddress, mouseButtonBytes, sizeof(mouseButtonBytes))
		&& BytesMatch(kCuiEquipHitTestAddress, hitTestBytes, sizeof(hitTestBytes))
		&& BytesMatch(kCuiEquipDrawInitialLimitImmediate - 2,
			drawInitialLimitBytes, sizeof(drawInitialLimitBytes))
		&& BytesMatch(kCuiEquipSlot51BlockedFillImmediate - 3,
			slot51BlockedFillBytes, sizeof(slot51BlockedFillBytes))
		&& BytesMatch(kCuiEquipDrawLoopLimitImmediate - 2,
			drawLoopLimitBytes, sizeof(drawLoopLimitBytes))
		&& BytesMatch(kSecondPendantExpirationRejectJump,
			expirationRejectJumpBytes, sizeof(expirationRejectJumpBytes))
		&& BytesMatch(kPetPanelInitialAnchor, petAnchorBytes, sizeof(petAnchorBytes))
		&& BytesMatch(kPetPanelMoveAnchor, petAnchorBytes, sizeof(petAnchorBytes))
		&& BytesMatch(kPetPanelShowAnchor, petAnchorBytes, sizeof(petAnchorBytes))
		&& NativeCoordinatesMatch()
		&& PointMatches(kNativeSecondPendantCoordinate, nativeSlot51Alias);
}

bool AttachHooks()
{
	if (DetourTransactionBegin() != NO_ERROR)
	{
		return false;
	}
	if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR
		|| DetourAttach(reinterpret_cast<void**>(&g_cuiEquipConstructor), CuiEquipConstructorHook) != NO_ERROR
		|| DetourAttach(reinterpret_cast<void**>(&g_cuiEquipHitTest), CuiEquipHitTestHook) != NO_ERROR
		|| DetourAttach(reinterpret_cast<void**>(&g_cuiEquipMouseButton), CuiEquipMouseButtonHook) != NO_ERROR)
	{
		DetourTransactionAbort();
		return false;
	}
	return DetourTransactionCommit() == NO_ERROR;
}

void WritePoint(DWORD address, SlotPoint point)
{
	Memory::WriteInt(address, static_cast<unsigned int>(point.x));
	Memory::WriteInt(address + sizeof(int), static_cast<unsigned int>(point.y));
}

void PatchNativeWindowWithoutExpandingIt()
{
	// The server capability remains enabled, but CUIEquip must never copy it into
	// the member that selects the wider background and expanded pet presentation.
	Memory::FillBytes(kCuiEquipExtraFlagStoreAddress, 0x90, 6);

	// The native renderer already contains complete slot-51 handling. Extend only
	// its two loop bounds, leaving all window/background state in native mode.
	Memory::WriteByte(kCuiEquipDrawInitialLimitImmediate, kSecondPendantSlot);
	Memory::WriteByte(kCuiEquipSlot51BlockedFillImmediate, 0);
	Memory::WriteByte(kCuiEquipDrawLoopLimitImmediate, kSecondPendantSlot);
	WritePoint(kNativeSecondPendantCoordinate, kSecondPendantPosition);
	if (SecondPendantRules::ShouldIgnoreExpansionExpirationForTarget(kSecondPendantSlot))
	{
		// The server enables this slot permanently for every character. Keep the
		// native item/slot validation, but do not reject slot 51 because the old
		// time-limited cash expansion timestamp was never initialized.
		Memory::FillBytes(kSecondPendantExpirationRejectJump, 0x90, 6);
	}

	FlushInstructionCache(GetCurrentProcess(),
		reinterpret_cast<const void*>(kCuiEquipExtraFlagStoreAddress), 6);
	FlushInstructionCache(GetCurrentProcess(),
		reinterpret_cast<const void*>(kCuiEquipDrawInitialLimitImmediate - 2), 3);
	FlushInstructionCache(GetCurrentProcess(),
		reinterpret_cast<const void*>(kCuiEquipSlot51BlockedFillImmediate - 3), 7);
	FlushInstructionCache(GetCurrentProcess(),
		reinterpret_cast<const void*>(kCuiEquipDrawLoopLimitImmediate - 2), 3);
	FlushInstructionCache(GetCurrentProcess(),
		reinterpret_cast<const void*>(kSecondPendantExpirationRejectJump), 6);
}
}

namespace SecondPendantSlot
{
void Install(bool enableLog)
{
	g_enableLog = enableLog;
	InitializeLogPath();

	if (!ValidateNativeLayout())
	{
		WriteEquipmentLog("install rejected reason=native_layout_mismatch");
		return;
	}

	if (!AttachHooks())
	{
		WriteEquipmentLog("install rejected reason=detour_attach_failed");
		return;
	}

	PatchNativeWindowWithoutExpandingIt();

	WriteEquipmentLog(
		"install ok mode=native-window pendant2=(%d,%d) drawMax=%d petOffset=%d permanent=1 log=%d",
		kSecondPendantPosition.x, kSecondPendantPosition.y,
		kSecondPendantSlot, kNativePetPanelOffset, enableLog ? 1 : 0);
}
}
