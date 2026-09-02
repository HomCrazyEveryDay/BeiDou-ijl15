#include "stdafx.h"
#include "ChairCompatibility.h"

#include <array>
#include <cstring>
#include <oaidl.h>

namespace {
constexpr DWORD kCUserUpdate = 0x00930B27;
constexpr DWORD kSetActivePortableChair = 0x0093C7C3;
constexpr size_t kBodyOriginOffset = 0x88 + 0x10B8;
constexpr size_t kRelMoveVtableIndex = 36;
constexpr int kFiveColorSeaChair = 3015060;
constexpr int kFiveColorSeaBodyX = -12;
constexpr int kFiveColorSeaBodyY = -112;
constexpr int kGiantPinkBeanChair = 3010070;
constexpr int kGiantPinkBeanBodyX = 0;
constexpr int kGiantPinkBeanBodyY = -200;

struct ChairOffset {
	void* user = nullptr;
	int x = 0;
	int y = 0;
};

std::array<ChairOffset, 64> g_offsets{};

using CUserUpdate = void(__thiscall*)(void* user);
using SetActivePortableChair = void(__thiscall*)(void* user, int itemId);
using RelMove = HRESULT(__stdcall*)(void* vector, int x, int y, VARIANT time, VARIANT type);

CUserUpdate g_userUpdate = reinterpret_cast<CUserUpdate>(kCUserUpdate);
SetActivePortableChair g_setActivePortableChair = reinterpret_cast<SetActivePortableChair>(kSetActivePortableChair);

ChairOffset* FindOffset(void* user)
{
	for (auto& offset : g_offsets) {
		if (offset.user == user) {
			return &offset;
		}
	}
	return nullptr;
}

void ClearOffset(void* user)
{
	if (auto* offset = FindOffset(user)) {
		*offset = {};
	}
}

void SetOffset(void* user, int x, int y)
{
	if (auto* offset = FindOffset(user)) {
		offset->x = x;
		offset->y = y;
		return;
	}

	for (auto& offset : g_offsets) {
		if (offset.user == nullptr) {
			offset = {user, x, y};
			return;
		}
	}
}

bool TryGetBodyOffset(int itemId, int& x, int& y)
{
	switch (itemId) {
	case kFiveColorSeaChair:
		x = kFiveColorSeaBodyX;
		y = kFiveColorSeaBodyY;
		return true;
	case kGiantPinkBeanChair:
		x = kGiantPinkBeanBodyX;
		y = kGiantPinkBeanBodyY;
		return true;
	default:
		return false;
	}
}

void ApplyBodyOffset(void* user, int x, int y)
{
	__try {
		void* vector = *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(user) + kBodyOriginOffset);
		if (vector == nullptr) {
			return;
		}

		void** vtable = *reinterpret_cast<void***>(vector);
		if (vtable == nullptr || vtable[kRelMoveVtableIndex] == nullptr) {
			return;
		}

		VARIANT missing{};
		missing.vt = VT_ERROR;
		missing.scode = DISP_E_PARAMNOTFOUND;
		reinterpret_cast<RelMove>(vtable[kRelMoveVtableIndex])(vector, x, y, missing, missing);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ClearOffset(user);
	}
}

void __fastcall CUserUpdateHook(void* user, void*)
{
	g_userUpdate(user);
	if (const auto* offset = FindOffset(user)) {
		ApplyBodyOffset(user, offset->x, offset->y);
	}
}

void __fastcall SetActivePortableChairHook(void* user, void*, int itemId)
{
	g_setActivePortableChair(user, itemId);
	int x = 0;
	int y = 0;
	if (TryGetBodyOffset(itemId, x, y)) {
		SetOffset(user, x, y);
	}
	else {
		ClearOffset(user);
	}
}

bool MatchesExpectedClient()
{
	const unsigned char updatePrologue[] = {0xB8, 0x63, 0xBC, 0xAD, 0x00, 0xE8};
	const unsigned char chairPrologue[] = {0xB8, 0x84, 0xCF, 0xAD, 0x00, 0xE8};
	return std::memcmp(reinterpret_cast<const void*>(kCUserUpdate), updatePrologue, sizeof(updatePrologue)) == 0
		&& std::memcmp(reinterpret_cast<const void*>(kSetActivePortableChair), chairPrologue, sizeof(chairPrologue)) == 0;
}
}

bool ChairCompatibility::Install()
{
	if (!MatchesExpectedClient()) {
		return false;
	}

	if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_userUpdate), CUserUpdateHook)) {
		return false;
	}
	if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_setActivePortableChair), SetActivePortableChairHook)) {
		Memory::SetHook(false, reinterpret_cast<void**>(&g_userUpdate), CUserUpdateHook);
		return false;
	}
	return true;
}
