#include "stdafx.h"
#include "ChairCompatibility.h"
#include "ClientLog.h"

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
	unsigned samples = 0;
	DWORD lastSample = 0;
};

std::array<ChairOffset, 64> g_offsets{};

void LogChair(const char* phase, void* user, int itemId, int x, int y,
	void* vector = nullptr, HRESULT result = S_OK)
{
	static const bool enabled = [] {
		char value[8]{};
		GetEnvironmentVariableA("BEIDOU_CHAIR_LOG", value, sizeof(value));
		return value[0] != '0';
	}();
	static unsigned records = 0;
	if (!enabled || records >= 256) return;
	const DWORD error = GetLastError();
	++records;
	ClientLog::Append(ClientLog::Component::Trace,
		"event=chair_offset phase=%s user=%p item=%d x=%d y=%d vector=%p hr=0x%08lX tick=%lu",
		phase, user, itemId, x, y, vector, static_cast<unsigned long>(result), GetTickCount());
	SetLastError(error);
}

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
		offset->samples = 0;
		offset->lastSample = 0;
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

void ApplyBodyOffset(void* user, int x, int y, bool sample)
{
	__try {
		void* vector = *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(user) + kBodyOriginOffset);
		if (vector == nullptr) {
			if (sample) LogChair("null_vector", user, 0, x, y);
			return;
		}

		void** vtable = *reinterpret_cast<void***>(vector);
		if (vtable == nullptr || vtable[kRelMoveVtableIndex] == nullptr) {
			if (sample) LogChair("null_relmove", user, 0, x, y, vector);
			return;
		}

		VARIANT missing{};
		missing.vt = VT_ERROR;
		missing.scode = DISP_E_PARAMNOTFOUND;
		const HRESULT result = reinterpret_cast<RelMove>(vtable[kRelMoveVtableIndex])(vector, x, y, missing, missing);
		if (sample) LogChair("relmove", user, 0, x, y, vector, result);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		LogChair("exception", user, 0, x, y, nullptr, static_cast<HRESULT>(GetExceptionCode()));
		ClearOffset(user);
	}
}

void __fastcall CUserUpdateHook(void* user, void*)
{
	g_userUpdate(user);
	if (auto* offset = FindOffset(user)) {
		const DWORD now = GetTickCount();
		const bool sample = offset->samples < 5 && (offset->samples == 0 || now - offset->lastSample >= 1000);
		if (sample) { ++offset->samples; offset->lastSample = now; }
		ApplyBodyOffset(user, offset->x, offset->y, sample);
	}
}

void __fastcall SetActivePortableChairHook(void* user, void*, int itemId)
{
	LogChair("set_begin", user, itemId, 0, 0);
	g_setActivePortableChair(user, itemId);
	int x = 0;
	int y = 0;
	if (TryGetBodyOffset(itemId, x, y)) {
		SetOffset(user, x, y);
		LogChair(FindOffset(user) ? "tracked" : "table_full", user, itemId, x, y);
	}
	else {
		ClearOffset(user);
		LogChair("cleared", user, itemId, 0, 0);
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
		LogChair("signature_mismatch", nullptr, 0, 0, 0);
		return false;
	}

	if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_userUpdate), CUserUpdateHook)) {
		LogChair("update_hook_failed", nullptr, 0, 0, 0);
		return false;
	}
	if (!Memory::SetHook(true, reinterpret_cast<void**>(&g_setActivePortableChair), SetActivePortableChairHook)) {
		LogChair("chair_hook_failed", nullptr, 0, 0, 0);
		Memory::SetHook(false, reinterpret_cast<void**>(&g_userUpdate), CUserUpdateHook);
		return false;
	}
	LogChair("installed", nullptr, 0, 0, 0);
	return true;
}
