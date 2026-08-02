#include "stdafx.h"
#include "RefreshRateTrace.h"

#include "CrashReporter.h"
#include "detours.h"

namespace {

using PcCreateObjectIWzPackage = void(__cdecl*)(int param1, DWORD param2, DWORD param3);
PcCreateObjectIWzPackage g_originalPcCreateObjectIWzPackage = nullptr;

LONG LogException(const char* phase, EXCEPTION_POINTERS* exceptionInfo)
{
	const DWORD code = exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr
		? exceptionInfo->ExceptionRecord->ExceptionCode
		: 0;
	const void* address = exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr
		? exceptionInfo->ExceptionRecord->ExceptionAddress
		: nullptr;
	CrashReporter::RecordEvent(
		"refreshRate.exception",
		"phase=%s code=0x%08lX address=%p",
		phase,
		code,
		address);
	return EXCEPTION_CONTINUE_SEARCH;
}

void __cdecl HookPcCreateObjectIWzPackage(int param1, DWORD param2, DWORD param3)
{
	CrashReporter::RecordEvent(
		"refreshRate",
		"original.begin param1=%d param2=0x%08lX param3=0x%08lX target=%p",
		param1,
		param2,
		param3,
		reinterpret_cast<void*>(g_originalPcCreateObjectIWzPackage));
	__try {
		g_originalPcCreateObjectIWzPackage(param1, param2, param3);
	}
	__except (LogException("original", GetExceptionInformation())) {
		return;
	}
	CrashReporter::RecordEvent("refreshRate", "original.end");

	__try {
		const DWORD refreshObject = *reinterpret_cast<DWORD*>(0x00BF14EC);
		if (refreshObject == 0) {
			CrashReporter::RecordEvent("refreshRate", "patch.skip object=null");
			return;
		}

		unsigned char* refreshRate = reinterpret_cast<unsigned char*>(refreshObject + 0x84);
		const unsigned char previousValue = *refreshRate;
		CrashReporter::RecordEvent(
			"refreshRate",
			"patch.begin object=0x%08lX field=%p old=%u new=60",
			refreshObject,
			refreshRate,
			static_cast<unsigned int>(previousValue));
		*refreshRate = 0x3C;
		CrashReporter::RecordEvent(
			"refreshRate",
			"patch.end object=0x%08lX field=%p value=%u",
			refreshObject,
			refreshRate,
			static_cast<unsigned int>(*refreshRate));
	}
	__except (LogException("patch", GetExceptionInformation())) {
		return;
	}
}

} // namespace

void RefreshRateTrace::Install()
{
	g_originalPcCreateObjectIWzPackage = reinterpret_cast<PcCreateObjectIWzPackage>(0x009FB0E9);
	const LONG beginResult = DetourTransactionBegin();
	const LONG updateResult = DetourUpdateThread(GetCurrentThread());
	const LONG attachResult = DetourAttach(
		reinterpret_cast<void**>(&g_originalPcCreateObjectIWzPackage),
		HookPcCreateObjectIWzPackage);
	const LONG commitResult = DetourTransactionCommit();
	CrashReporter::RecordEvent(
		"refreshRate",
		"install begin=%ld update=%ld attach=%ld commit=%ld trampoline=%p",
		beginResult,
		updateResult,
		attachResult,
		commitResult,
		reinterpret_cast<void*>(g_originalPcCreateObjectIWzPackage));
}
