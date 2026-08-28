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
constexpr DWORD kCharacterDataOffset = 0x000020B8;
constexpr DWORD kSecondPendantExpirationOffset = 0x000002C7
	+ SecondPendantRules::kSecondPendantSlot * sizeof(unsigned long long);
static_assert(kSecondPendantExpirationOffset == 0x0000045F,
	"slot-51 expansion expiration must match the native CharacterData field");
constexpr DWORD kCuiEquipExtraFlagOffset = 0x000005E8;

constexpr DWORD kWvsContextSetExtraPendantSlotAddress = 0x00A13C6C;
constexpr DWORD kWvsContextRecalculateStatsAddress = 0x00A0843C;
constexpr DWORD kCalculateDerivedStatsAddress = 0x0077F4C9;

constexpr DWORD kCuiEquipConstructorAddress = 0x007FDE7C;
constexpr DWORD kCuiEquipExtraFlagStoreAddress = 0x007FDEFB;
constexpr DWORD kCuiEquipMouseButtonAddress = 0x007FE4C6;
constexpr DWORD kCuiEquipHitTestAddress = 0x007FEC32;
constexpr DWORD kCuiEquipDrawAddress = 0x007FEC81;
constexpr DWORD kCuiEquipDrawInitialLimitImmediate = 0x007FEE56;
constexpr DWORD kCuiEquipSlot51BlockedFillImmediate = 0x007FEEB8;
constexpr DWORD kCuiEquipSlot51ExpirationValidJump = 0x007FEEB3;
constexpr DWORD kCuiEquipDrawLoopLimitImmediate = 0x007FEFBE;
constexpr DWORD kSecondPendantExpirationRejectJump = 0x004F1CCA;
constexpr DWORD kSecondPendantExpiredTooltipAssignCall = 0x008F1F7F;
constexpr DWORD kSecondPendantActiveTooltipFormatCall = 0x008F1FC6;
constexpr DWORD kZxStringAssignTextAddress = 0x00414617;
constexpr DWORD kZxStringFormatAddress = 0x00445B4B;

const char kSecondPendantExpiredTooltipText[] =
	"\xB7\xC5\xC8\xEB\xBD\xC7\xC9\xAB\xCF\xD6\xBD\xF0\xB5\xC0\xBE\xDF"
	"\xC0\xB8\xBA\xF3\xA3\xAC\xBF\xC9\xBF\xAA\xB7\xC5\xC0\xA9\xB3\xE4"
	"\xCF\xEE\xC1\xB4\xC0\xB8\xCE\xBB\x33\x30\xCC\xEC\xA1\xA3\x0D\x0A"
	"\xB3\xD6\xD3\xD0\xB6\xE0\xD5\xC5\xCA\xB1\xC8\xA1\xD7\xEE\xCD\xED"
	"\xB5\xBD\xC6\xDA\xCA\xB1\xBC\xE4\xA3\xAC\xB2\xBB\xC0\xDB\xBC\xC6"
	"\xD1\xD3\xB3\xA4\xA1\xA3";
const char kSecondPendantActiveTooltipText[] =
	"\xB7\xC5\xC8\xEB\xBD\xC7\xC9\xAB\xCF\xD6\xBD\xF0\xB5\xC0\xBE\xDF"
	"\xC0\xB8\xBA\xF3\xA3\xAC\xBF\xC9\xBF\xAA\xB7\xC5\xC0\xA9\xB3\xE4"
	"\xCF\xEE\xC1\xB4\xC0\xB8\xCE\xBB\x33\x30\xCC\xEC\xA1\xA3\x0D\x0A"
	"\xB3\xD6\xD3\xD0\xB6\xE0\xD5\xC5\xCA\xB1\xC8\xA1\xD7\xEE\xCD\xED"
	"\xB5\xBD\xC6\xDA\xCA\xB1\xBC\xE4\xA3\xAC\xB2\xBB\xC0\xDB\xBC\xC6"
	"\xD1\xD3\xB3\xA4\xA1\xA3";
const char kSecondPendantLockedTooltipText[] =
	"\xBB\xF1\xB5\xC3\xCF\xEE\xC1\xB4\xC0\xA9\xB3\xE4\xB5\xC0\xBE\xDF"
	"\xBA\xF3\xA3\xAC\xB8\xC3\xD7\xB0\xB1\xB8\xBD\xAB\xBB\xD6\xB8\xB4"
	"\xC9\xFA\xD0\xA7\xA1\xA3";
const char kSecondPendantPurchaseConfirmationText[] =
	"\xB9\xBA\xC2\xF2\x25\x73\xD0\xE8\xD2\xAA\x25\x64\xB5\xE3\xC8\xAF\xA1\xA3\x0D\x0A"
	"\xBF\xC9\xCA\xB9\xD3\xC3\xC0\xA9\xB3\xE4\xCF\xEE\xC1\xB4\xC0\xB8\xCE\xBB"
	"\x25\x64\xCC\xEC\xA1\xA3\x0D\x0A\xB9\xBA\xC2\xF2\xBA\xF3\xCE\xDE\xB7\xA8"
	"\xCD\xCB\xBF\xEE\xA1\xA3";

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
constexpr size_t kEquipmentSnapshotEntryCount =
	SecondPendantRules::kEquipmentSnapshotEntryCount;
constexpr size_t kEquipmentSnapshotSize = SecondPendantRules::kEquipmentSnapshotSize;

struct EquipmentSnapshotEntry
{
	DWORD reference;
	void* item;
};

struct CInPacket
{
	int loopback;
	int state;
	void* data;
	unsigned short dataLength;
	unsigned short rawSequence;
	unsigned int unknown;
	unsigned int offset;
};

static_assert(offsetof(CInPacket, dataLength) == 0x0C,
	"unexpected CInPacket data length offset");
static_assert(offsetof(CInPacket, offset) == 0x14,
	"unexpected CInPacket read offset");

static_assert(sizeof(EquipmentSnapshotEntry) == SecondPendantRules::kEquipmentSnapshotEntrySize,
	"the native equipment snapshot entry must remain eight bytes");
static_assert(kEquipmentSnapshotEntryCount == 52,
	"the native equipment snapshot must include indices 0 through 51");
static_assert(kEquipmentSnapshotSize == 0x1A0,
	"the native equipment snapshot must remain 0x1A0 bytes");
static_assert(SecondPendantRules::EquipmentSnapshotEntryOffset(51) == 0x198,
	"slot 51 must remain at equipment snapshot offset 0x198");
static_assert(SecondPendantRules::EquipmentSnapshotItemPointerOffset(51) == 0x19C,
	"the slot-51 item pointer must remain at equipment snapshot offset 0x19C");

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
volatile LONG g_lastBlockedState = -1;
volatile LONG g_loggedActiveTooltip = 0;
volatile LONG g_loggedExpiredTooltip = 0;
volatile LONG g_loggedLockedTooltip = 0;
volatile LONG g_loggedPurchaseConfirmation = 0;
volatile LONG g_loggedUnknownExpansionTooltip = 0;

using CuiEquipConstructor = void* (__fastcall*)(void* pThis, void* edx);
using CuiEquipHitTest = int(__fastcall*)(void* pThis, void* edx, int x, int y);
using CuiEquipMouseButton = void(__fastcall*)(void* pThis, void* edx, unsigned int message,
	unsigned int wParam, int x, int y);
using CuiEquipDraw = void(__fastcall*)(void* pThis, void* edx, const void* rect);
using WvsContextSetExtraPendantSlot = void(__thiscall*)(void* pThis, CInPacket* packet);
using WvsContextRecalculateStats = void(__thiscall*)(void* pThis);
using CalculateDerivedStats = void(__thiscall*)(void* pThis, void* characterData,
	void* basicStats, void* temporaryStats, void* normalEquipment,
	void* cashEquipment, void* petEquipment);
using ZxStringAssignText = void(__thiscall*)(void* pThis, const char* text, int length);
using ZxStringFormat = void* (__cdecl*)(void* result, const char* format, ...);

CuiEquipConstructor g_cuiEquipConstructor = reinterpret_cast<CuiEquipConstructor>(kCuiEquipConstructorAddress);
CuiEquipHitTest g_cuiEquipHitTest = reinterpret_cast<CuiEquipHitTest>(kCuiEquipHitTestAddress);
CuiEquipMouseButton g_cuiEquipMouseButton = reinterpret_cast<CuiEquipMouseButton>(kCuiEquipMouseButtonAddress);
CuiEquipDraw g_cuiEquipDraw = reinterpret_cast<CuiEquipDraw>(kCuiEquipDrawAddress);
WvsContextSetExtraPendantSlot g_wvsContextSetExtraPendantSlot =
	reinterpret_cast<WvsContextSetExtraPendantSlot>(kWvsContextSetExtraPendantSlotAddress);
WvsContextRecalculateStats g_wvsContextRecalculateStats =
	reinterpret_cast<WvsContextRecalculateStats>(kWvsContextRecalculateStatsAddress);
CalculateDerivedStats g_calculateDerivedStats =
	reinterpret_cast<CalculateDerivedStats>(kCalculateDerivedStatsAddress);
ZxStringAssignText g_zxStringAssignText =
	reinterpret_cast<ZxStringAssignText>(kZxStringAssignTextAddress);
ZxStringFormat g_zxStringFormat = reinterpret_cast<ZxStringFormat>(kZxStringFormatAddress);

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

bool ShouldMaskCurrentCharacterDerivedStats(void* characterData)
{
	if (characterData == nullptr)
	{
		return false;
	}

	__try
	{
		const DWORD context = *reinterpret_cast<const DWORD*>(kWvsContextSingleton);
		if (context == 0
			|| *reinterpret_cast<void**>(context + kCharacterDataOffset) != characterData)
		{
			return false;
		}

		const bool enabled = *reinterpret_cast<const DWORD*>(
			context + kExtraPendantFlagOffset) != 0;
		return SecondPendantRules::ShouldMaskDerivedEquipmentSnapshot(
			enabled, kSecondPendantSlot);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

void __fastcall CalculateDerivedStatsHook(void* pThis, void* edx, void* characterData,
	void* basicStats, void* temporaryStats, void* normalEquipment,
	void* cashEquipment, void* petEquipment)
{
	if (!ShouldMaskCurrentCharacterDerivedStats(characterData)
		|| normalEquipment == nullptr || cashEquipment == nullptr)
	{
		g_calculateDerivedStats(pThis, characterData, basicStats, temporaryStats,
			normalEquipment, cashEquipment, petEquipment);
		return;
	}

	std::array<EquipmentSnapshotEntry, kEquipmentSnapshotEntryCount> normalSnapshot{};
	std::array<EquipmentSnapshotEntry, kEquipmentSnapshotEntryCount> cashSnapshot{};
	memcpy(normalSnapshot.data(), normalEquipment, sizeof(normalSnapshot));
	memcpy(cashSnapshot.data(), cashEquipment, sizeof(cashSnapshot));

	void* normalItem = normalSnapshot[kSecondPendantSlot].item;
	void* cashItem = cashSnapshot[kSecondPendantSlot].item;
	normalSnapshot[kSecondPendantSlot].item = nullptr;
	cashSnapshot[kSecondPendantSlot].item = nullptr;

	g_calculateDerivedStats(pThis, characterData, basicStats, temporaryStats,
		normalSnapshot.data(), cashSnapshot.data(), petEquipment);
	if (normalItem != nullptr || cashItem != nullptr)
	{
		WriteEquipmentLog("derived snapshot masked normal=%08X cash=%08X persistent=untouched",
			reinterpret_cast<DWORD>(normalItem), reinterpret_cast<DWORD>(cashItem));
	}
}

bool TryReadServerExpansionExpiration(CInPacket* packet, unsigned long long* expiration)
{
	if (packet == nullptr || expiration == nullptr || packet->data == nullptr
		|| packet->offset > packet->dataLength
		|| packet->dataLength - packet->offset < sizeof(*expiration))
	{
		return false;
	}

	memcpy(expiration,
		reinterpret_cast<const unsigned char*>(packet->data) + packet->offset,
		sizeof(*expiration));
	packet->offset += sizeof(*expiration);
	return true;
}

void SyncSecondPendantExpiration(void* context, bool hasServerExpiration,
	unsigned long long serverExpiration)
{
	if (context == nullptr)
	{
		return;
	}

	__try
	{
		const DWORD characterData = *reinterpret_cast<const DWORD*>(
			reinterpret_cast<const BYTE*>(context) + kCharacterDataOffset);
		if (characterData == 0)
		{
			WriteEquipmentLog("expiration sync skipped reason=no_character_data");
			return;
		}

		const bool enabled = *reinterpret_cast<const DWORD*>(
			reinterpret_cast<const BYTE*>(context) + kExtraPendantFlagOffset) != 0;
		unsigned long long* expiration = reinterpret_cast<unsigned long long*>(
			characterData + kSecondPendantExpirationOffset);
		const unsigned long long previous = *expiration;
		const unsigned long long current = SecondPendantRules::ExpansionTimeForAccess(
			enabled, hasServerExpiration, serverExpiration);
		if (previous == current)
		{
			WriteEquipmentLog("expiration unchanged enabled=%d source=%s value=%I64u recalculated=0",
				enabled ? 1 : 0, hasServerExpiration ? "server" : "fallback", current);
			return;
		}
		*expiration = current;
		g_wvsContextRecalculateStats(context);

		WriteEquipmentLog("expiration synced enabled=%d source=%s old=%I64u new=%I64u recalculated=1",
			enabled ? 1 : 0, hasServerExpiration ? "server" : "fallback", previous, current);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		WriteEquipmentLog("expiration sync failed reason=memory_access");
	}
}

bool IsInsideSecondPendant(int x, int y)
{
	return x > kSecondPendantPosition.x && x < kSecondPendantPosition.x + kSlotSize
		&& y > kSecondPendantPosition.y && y < kSecondPendantPosition.y + kSlotSize;
}

int ResolveEquipmentSlot(void* pThis, void* edx, int x, int y)
{
	// Keep a locked, occupied slot inspectable and removable. The server still
	// rejects every attempt to equip into slot 51 while access is disabled.
	if (IsInsideSecondPendant(x, y))
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

void SetSecondPendantBlocked(bool blocked)
{
	const LONG state = blocked ? 1 : 0;
	if (InterlockedExchange(&g_lastBlockedState, state) == state)
	{
		return;
	}

	if (blocked)
	{
		// Force the native slot-51 renderer down its expired/disabled branch.
		Memory::WriteByte(kCuiEquipSlot51ExpirationValidJump, 0x90);
		Memory::WriteByte(kCuiEquipSlot51ExpirationValidJump + 1, 0x90);
	}
	else
	{
		// Skip the expired branch while the server says the access item is present.
		Memory::WriteByte(kCuiEquipSlot51ExpirationValidJump, 0xEB);
		Memory::WriteByte(kCuiEquipSlot51ExpirationValidJump + 1, 0x09);
	}
	FlushInstructionCache(GetCurrentProcess(),
		reinterpret_cast<const void*>(kCuiEquipSlot51ExpirationValidJump), 2);
	WriteEquipmentLog("access state enabled=%d blocked=%d", blocked ? 0 : 1, blocked ? 1 : 0);
}

void __fastcall CuiEquipDrawHook(void* pThis, void* edx, const void* rect)
{
	SetSecondPendantBlocked(!IsExtraPendantEnabled());
	g_cuiEquipDraw(pThis, edx, rect);
}

void __fastcall WvsContextSetExtraPendantSlotHook(void* pThis, void* edx, CInPacket* packet)
{
	g_wvsContextSetExtraPendantSlot(pThis, packet);
	unsigned long long serverExpiration = 0;
	const bool hasServerExpiration = TryReadServerExpansionExpiration(packet, &serverExpiration);
	SyncSecondPendantExpiration(pThis, hasServerExpiration, serverExpiration);
}

void* __fastcall AssignSecondPendantExpiredTooltip(void* result, void* edx, void* source)
{
	g_zxStringAssignText(result, kSecondPendantExpiredTooltipText, -1);
	WriteEquipmentLog("tooltip localized state=expired finalAssign=008F1F7F");
	return result;
}

void* __cdecl FormatSecondPendantActiveTooltip(void* result, const char* sourceFormat,
	int year, int month, int day, int hour, int minute)
{
	WriteEquipmentLog(
		"tooltip localized state=active finalFormat=008F1FC6 date=%04d-%02d-%02d %02d:%02d",
		year, month, day, hour, minute);
	return g_zxStringFormat(result, kSecondPendantActiveTooltipText,
		year, month, day, hour, minute);
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
	const unsigned char drawBytes[] = { 0xB8, 0xEC, 0xF7, 0xAB, 0x00 };
	const unsigned char drawInitialLimitBytes[] = { 0x83, 0xC1, 0x32 };
	const unsigned char slot51BlockedFillBytes[] = { 0xC7, 0x45, 0xE8, 0x01, 0x00, 0x00, 0x00 };
	const unsigned char slot51ExpirationValidJumpBytes[] = { 0x7D, 0x09 };
	const unsigned char drawLoopLimitBytes[] = { 0x83, 0xC0, 0x32 };
	const unsigned char expirationRejectJumpBytes[] = { 0x0F, 0x8C, 0xA3, 0x0E, 0x00, 0x00 };
	const unsigned char expiredTooltipAssignCallBytes[] = { 0xE8, 0x45, 0x62, 0xB2, 0xFF };
	const unsigned char activeTooltipFormatCallBytes[] = { 0xE8, 0x80, 0x3B, 0xB5, 0xFF };
	const unsigned char setExtraPendantSlotBytes[] = { 0x56, 0x8B, 0xF1, 0x8B, 0x4C, 0x24, 0x08 };
	const unsigned char recalculateStatsBytes[] = { 0xB8, 0xF7, 0x9A, 0xAE, 0x00 };
	const unsigned char calculateDerivedStatsBytes[] = { 0xB8, 0x7F, 0x63, 0xAB, 0x00 };
	const unsigned char petAnchorBytes[] = { 0x05, 0xAC, 0x00, 0x00, 0x00 };
	const SlotPoint nativeSlot51Alias{ 38, 35 };

	return BytesMatch(kCuiEquipConstructorAddress, constructorBytes, sizeof(constructorBytes))
		&& BytesMatch(kCuiEquipExtraFlagStoreAddress, extraFlagStoreBytes, sizeof(extraFlagStoreBytes))
		&& BytesMatch(kCuiEquipMouseButtonAddress, mouseButtonBytes, sizeof(mouseButtonBytes))
		&& BytesMatch(kCuiEquipHitTestAddress, hitTestBytes, sizeof(hitTestBytes))
		&& BytesMatch(kCuiEquipDrawAddress, drawBytes, sizeof(drawBytes))
		&& BytesMatch(kCuiEquipDrawInitialLimitImmediate - 2,
			drawInitialLimitBytes, sizeof(drawInitialLimitBytes))
		&& BytesMatch(kCuiEquipSlot51BlockedFillImmediate - 3,
			slot51BlockedFillBytes, sizeof(slot51BlockedFillBytes))
		&& BytesMatch(kCuiEquipSlot51ExpirationValidJump,
			slot51ExpirationValidJumpBytes, sizeof(slot51ExpirationValidJumpBytes))
		&& BytesMatch(kCuiEquipDrawLoopLimitImmediate - 2,
			drawLoopLimitBytes, sizeof(drawLoopLimitBytes))
		&& BytesMatch(kSecondPendantExpirationRejectJump,
			expirationRejectJumpBytes, sizeof(expirationRejectJumpBytes))
		&& BytesMatch(kSecondPendantExpiredTooltipAssignCall,
			expiredTooltipAssignCallBytes, sizeof(expiredTooltipAssignCallBytes))
		&& BytesMatch(kSecondPendantActiveTooltipFormatCall,
			activeTooltipFormatCallBytes, sizeof(activeTooltipFormatCallBytes))
		&& BytesMatch(kWvsContextSetExtraPendantSlotAddress,
			setExtraPendantSlotBytes, sizeof(setExtraPendantSlotBytes))
		&& BytesMatch(kWvsContextRecalculateStatsAddress,
			recalculateStatsBytes, sizeof(recalculateStatsBytes))
		&& BytesMatch(kCalculateDerivedStatsAddress,
			calculateDerivedStatsBytes, sizeof(calculateDerivedStatsBytes))
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
		|| DetourAttach(reinterpret_cast<void**>(&g_cuiEquipMouseButton), CuiEquipMouseButtonHook) != NO_ERROR
		|| DetourAttach(reinterpret_cast<void**>(&g_cuiEquipDraw), CuiEquipDrawHook) != NO_ERROR
		|| DetourAttach(reinterpret_cast<void**>(&g_wvsContextSetExtraPendantSlot),
			WvsContextSetExtraPendantSlotHook) != NO_ERROR
		|| DetourAttach(reinterpret_cast<void**>(&g_calculateDerivedStats),
			CalculateDerivedStatsHook) != NO_ERROR)
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

void PatchTooltipCall(DWORD address, const void* target)
{
	Memory::WriteByte(address, 0xE8);
	Memory::WriteInt(address + 1,
		reinterpret_cast<DWORD>(target) - (address + 5));
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(address), 5);
}

DWORD ReadRelativeCallTarget(DWORD address)
{
	const LONG displacement = *reinterpret_cast<const LONG*>(address + 1);
	return address + 5 + displacement;
}

void PatchNativeWindowWithoutExpandingIt()
{
	// The server capability controls slot access, but CUIEquip must never copy it into
	// the member that selects the wider background and expanded pet presentation.
	Memory::FillBytes(kCuiEquipExtraFlagStoreAddress, 0x90, 6);

	// The native renderer already contains complete slot-51 handling. Extend only
	// its two loop bounds, leaving all window/background state in native mode.
	Memory::WriteByte(kCuiEquipDrawInitialLimitImmediate, kSecondPendantSlot);
	Memory::WriteByte(kCuiEquipDrawLoopLimitImmediate, kSecondPendantSlot);
	WritePoint(kNativeSecondPendantCoordinate, kSecondPendantPosition);
	PatchTooltipCall(kSecondPendantExpiredTooltipAssignCall,
		AssignSecondPendantExpiredTooltip);
	PatchTooltipCall(kSecondPendantActiveTooltipFormatCall,
		FormatSecondPendantActiveTooltip);
	const DWORD expiredTarget = ReadRelativeCallTarget(kSecondPendantExpiredTooltipAssignCall);
	const DWORD activeTarget = ReadRelativeCallTarget(kSecondPendantActiveTooltipFormatCall);
	WriteEquipmentLog(
		"tooltip patch installed expiredCall=%08X target=%08X expected=%08X verified=%d "
		"activeCall=%08X target=%08X expected=%08X verified=%d",
		kSecondPendantExpiredTooltipAssignCall, expiredTarget,
		reinterpret_cast<DWORD>(AssignSecondPendantExpiredTooltip),
		expiredTarget == reinterpret_cast<DWORD>(AssignSecondPendantExpiredTooltip) ? 1 : 0,
		kSecondPendantActiveTooltipFormatCall, activeTarget,
		reinterpret_cast<DWORD>(FormatSecondPendantActiveTooltip),
		activeTarget == reinterpret_cast<DWORD>(FormatSecondPendantActiveTooltip) ? 1 : 0);
	if (SecondPendantRules::ShouldIgnoreExpansionExpirationForTarget(kSecondPendantSlot))
	{
		// Access is enforced by the live server flag and again by the server equip
		// handler. Ignore only the obsolete client timestamp so gaining item
		// 5550000 can unlock the slot without reconnecting.
		Memory::FillBytes(kSecondPendantExpirationRejectJump, 0x90, 6);
	}
	SetSecondPendantBlocked(!IsExtraPendantEnabled());

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
const char* LocalizeStringPoolTooltip(unsigned int stringId, const char* text)
{
	if (text == nullptr)
	{
		return nullptr;
	}

	const bool mentionsCslot = strstr(text, "cslot") != nullptr;
	const bool mentionsExpiration = strstr(text, "expiration") != nullptr
		|| strstr(text, "expired") != nullptr;
	const bool mentionsSlotExtender = strstr(text, "slot extender") != nullptr;
	const bool purchaseConfirmation = SecondPendantRules::IsPurchaseConfirmationTemplate(text);
	if (!mentionsCslot && !mentionsExpiration && !mentionsSlotExtender && !purchaseConfirmation)
	{
		return nullptr;
	}

	const bool active = strstr(text, "effects will disappear") != nullptr;
	const bool expired = strstr(text, "effects have disappeared") != nullptr
		|| strstr(text, "effects will not be applied") != nullptr;
	const bool locked = SecondPendantRules::IsLockedTooltipTemplate(text);
	volatile LONG* logFlag = active ? &g_loggedActiveTooltip
		: expired ? &g_loggedExpiredTooltip
		: locked ? &g_loggedLockedTooltip
		: purchaseConfirmation ? &g_loggedPurchaseConfirmation
		: &g_loggedUnknownExpansionTooltip;
	const bool shouldLog = InterlockedCompareExchange(logFlag, 1, 0) == 0;
	if (shouldLog)
	{
		WriteEquipmentLog(
			"tooltip StringPool candidate id=%u active=%d expired=%d locked=%d purchase=%d text=%s",
			stringId, active ? 1 : 0, expired ? 1 : 0, locked ? 1 : 0,
			purchaseConfirmation ? 1 : 0, text);
	}
	if (active)
	{
		if (shouldLog)
		{
			WriteEquipmentLog("tooltip StringPool replaced id=%u state=active", stringId);
		}
		return kSecondPendantActiveTooltipText;
	}
	if (expired)
	{
		if (shouldLog)
		{
			WriteEquipmentLog("tooltip StringPool replaced id=%u state=expired", stringId);
		}
		return kSecondPendantExpiredTooltipText;
	}
	if (locked)
	{
		if (shouldLog)
		{
			WriteEquipmentLog("tooltip StringPool replaced id=%u state=locked", stringId);
		}
		return kSecondPendantLockedTooltipText;
	}
	if (purchaseConfirmation)
	{
		if (shouldLog)
		{
			WriteEquipmentLog("tooltip StringPool replaced id=%u state=purchase", stringId);
		}
		return kSecondPendantPurchaseConfirmationText;
	}
	return nullptr;
}

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
		"install ok mode=native-window pendant2=(%d,%d) drawMax=%d petOffset=%d "
		"accessItem=5550000 tooltipPatch=final-call log=%d",
		kSecondPendantPosition.x, kSecondPendantPosition.y,
		kSecondPendantSlot, kNativePetPanelOffset, enableLog ? 1 : 0);
}
}
