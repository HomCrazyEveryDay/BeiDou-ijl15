#include "stdafx.h"
#include "LauncherGate.h"
#include <TlHelp32.h>
#include <cstdarg>
#include <cwchar>

namespace
{
	constexpr wchar_t TokenEnvironmentVariable[] = L"ZHUMENG_LAUNCH_TOKEN";
	constexpr wchar_t PermitNamePrefix[] = L"Local\\ZhuMengLaunchPermit-";
	constexpr wchar_t AcknowledgementNamePrefix[] = L"Local\\ZhuMengLaunchAck-";
	constexpr wchar_t LauncherExecutable[] = L"ZhuMengLauncher.exe";
	constexpr size_t TokenLength = 64;
	bool g_showUnauthorizedLaunchMessage = true;

	void WriteAuthorizationLog(HANDLE file, const wchar_t* format, ...)
	{
		if (file == INVALID_HANDLE_VALUE)
		{
			return;
		}

		wchar_t message[2048]{};
		va_list arguments;
		va_start(arguments, format);
		const int messageLength = _vsnwprintf_s(
			message,
			_countof(message),
			_TRUNCATE,
			format,
			arguments);
		va_end(arguments);
		if (messageLength <= 0)
		{
			return;
		}

		SYSTEMTIME now{};
		GetLocalTime(&now);
		wchar_t line[2304]{};
		const int lineLength = swprintf_s(
			line,
			L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s\r\n",
			now.wYear,
			now.wMonth,
			now.wDay,
			now.wHour,
			now.wMinute,
			now.wSecond,
			now.wMilliseconds,
			message);
		if (lineLength <= 0)
		{
			return;
		}

		char utf8[8192]{};
		const int utf8Length = WideCharToMultiByte(
			CP_UTF8,
			0,
			line,
			lineLength,
			utf8,
			static_cast<int>(sizeof(utf8)),
			nullptr,
			nullptr);
		if (utf8Length <= 0)
		{
			return;
		}

		DWORD written = 0;
		WriteFile(file, utf8, static_cast<DWORD>(utf8Length), &written, nullptr);
	}

	HANDLE CreateAuthorizationLog()
	{
		wchar_t executablePath[MAX_PATH]{};
		const DWORD executableLength = GetModuleFileNameW(
			nullptr,
			executablePath,
			_countof(executablePath));
		if (executableLength == 0 || executableLength >= _countof(executablePath))
		{
			return INVALID_HANDLE_VALUE;
		}

		wchar_t* separator = wcsrchr(executablePath, L'\\');
		if (separator == nullptr)
		{
			return INVALID_HANDLE_VALUE;
		}
		*separator = L'\0';

		wchar_t logDirectory[MAX_PATH]{};
		if (swprintf_s(logDirectory, L"%s\\logs", executablePath) <= 0)
		{
			return INVALID_HANDLE_VALUE;
		}
		if (!CreateDirectoryW(logDirectory, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
		{
			return INVALID_HANDLE_VALUE;
		}

		SYSTEMTIME now{};
		GetLocalTime(&now);
		wchar_t logPath[MAX_PATH]{};
		if (swprintf_s(
				logPath,
				L"%s\\ijl15-launch-auth-%04u%02u%02u-%02u%02u%02u-%lu.log",
				logDirectory,
				now.wYear,
				now.wMonth,
				now.wDay,
				now.wHour,
				now.wMinute,
				now.wSecond,
				GetCurrentProcessId()) <= 0)
		{
			return INVALID_HANDLE_VALUE;
		}

		return CreateFileW(
			logPath,
			GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
	}

	bool FinishAuthorization(
		HANDLE log,
		bool authorized,
		const wchar_t* stage,
		DWORD error = ERROR_SUCCESS)
	{
		WriteAuthorizationLog(
			log,
			L"result=%s stage=%s win32Error=%lu",
			authorized ? L"authorized" : L"rejected",
			stage,
			error);
		if (log != INVALID_HANDLE_VALUE)
		{
			CloseHandle(log);
		}
		return authorized;
	}

	bool IsValidToken(const wchar_t* token)
	{
		if (wcslen(token) != TokenLength)
		{
			return false;
		}

		for (size_t index = 0; index < TokenLength; ++index)
		{
			const wchar_t character = token[index];
			if (!((character >= L'0' && character <= L'9')
				|| (character >= L'a' && character <= L'f')))
			{
				return false;
			}
		}

		return true;
	}

	bool TryGetParentProcessId(DWORD& parentProcessId, DWORD& error)
	{
		const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			return false;
		}

		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		bool found = false;
		if (Process32FirstW(snapshot, &entry))
		{
			do
			{
				if (entry.th32ProcessID == GetCurrentProcessId())
				{
					parentProcessId = entry.th32ParentProcessID;
					found = parentProcessId != 0;
					break;
				}
			} while (Process32NextW(snapshot, &entry));
		}
		else
		{
			error = GetLastError();
		}

		CloseHandle(snapshot);
		if (!found && error == ERROR_SUCCESS)
		{
			error = ERROR_NOT_FOUND;
		}
		return found;
	}

	bool TryGetParentProcessPath(
		wchar_t* path,
		DWORD capacity,
		DWORD& parentProcessId,
		DWORD& error)
	{
		if (!TryGetParentProcessId(parentProcessId, error))
		{
			return false;
		}

		const HANDLE parent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentProcessId);
		if (parent == nullptr)
		{
			error = GetLastError();
			return false;
		}

		DWORD length = capacity;
		const BOOL succeeded = QueryFullProcessImageNameW(parent, 0, path, &length);
		error = succeeded ? ERROR_SUCCESS : GetLastError();
		CloseHandle(parent);
		if (!succeeded || length == 0 || length >= capacity)
		{
			if (succeeded)
			{
				error = ERROR_INSUFFICIENT_BUFFER;
			}
			return false;
		}
		return true;
	}

	bool TryGetExpectedLauncherPath(wchar_t* path, DWORD capacity, DWORD& error)
	{
		const DWORD length = GetModuleFileNameW(nullptr, path, capacity);
		if (length == 0 || length >= capacity)
		{
			error = length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
			return false;
		}

		wchar_t* separator = wcsrchr(path, L'\\');
		if (separator == nullptr)
		{
			error = ERROR_INVALID_NAME;
			return false;
		}

		const size_t remaining = capacity - static_cast<size_t>((separator + 1) - path);
		if (wcscpy_s(separator + 1, remaining, LauncherExecutable) != 0)
		{
			error = ERROR_INSUFFICIENT_BUFFER;
			return false;
		}
		error = ERROR_SUCCESS;
		return true;
	}

	bool HasExpectedParent(HANDLE log, DWORD& error)
	{
		wchar_t parentPath[32768]{};
		wchar_t expectedPath[32768]{};
		DWORD parentProcessId = 0;
		if (!TryGetParentProcessPath(
				parentPath,
				_countof(parentPath),
				parentProcessId,
				error))
		{
			WriteAuthorizationLog(
				log,
				L"parent inspection failed stage=query-parent pid=%lu win32Error=%lu",
				parentProcessId,
				error);
			return false;
		}
		if (!TryGetExpectedLauncherPath(expectedPath, _countof(expectedPath), error))
		{
			WriteAuthorizationLog(
				log,
				L"parent inspection failed stage=expected-path pid=%lu win32Error=%lu",
				parentProcessId,
				error);
			return false;
		}

		WriteAuthorizationLog(log, L"parentPid=%lu", parentProcessId);
		WriteAuthorizationLog(log, L"parentPath=%s", parentPath);
		WriteAuthorizationLog(log, L"expectedParentPath=%s", expectedPath);
		if (CompareStringOrdinal(parentPath, -1, expectedPath, -1, TRUE) != CSTR_EQUAL)
		{
			error = ERROR_BAD_PATHNAME;
			return false;
		}

		error = ERROR_SUCCESS;
		return true;
	}

	bool BuildObjectName(
		wchar_t* destination,
		size_t capacity,
		const wchar_t* prefix,
		const wchar_t* token)
	{
		return wcscpy_s(destination, capacity, prefix) == 0
			&& wcscat_s(destination, capacity, token) == 0;
	}
}

bool LauncherGate::Authorize()
{
	g_showUnauthorizedLaunchMessage = true;
	const HANDLE log = CreateAuthorizationLog();
	WriteAuthorizationLog(log, L"authorization begin pid=%lu", GetCurrentProcessId());

	wchar_t token[TokenLength + 1]{};
	const DWORD tokenLength = GetEnvironmentVariableW(
		TokenEnvironmentVariable,
		token,
		_countof(token));
	const DWORD tokenError = tokenLength == 0 ? GetLastError() : ERROR_SUCCESS;
	SetEnvironmentVariableW(TokenEnvironmentVariable, nullptr);
	WriteAuthorizationLog(log, L"tokenLength=%lu", tokenLength);
	if (tokenLength == 0)
	{
		SecureZeroMemory(token, sizeof(token));
		return FinishAuthorization(log, false, L"token-missing", tokenError);
	}
	if (tokenLength != TokenLength || !IsValidToken(token))
	{
		SecureZeroMemory(token, sizeof(token));
		return FinishAuthorization(log, false, L"token-invalid", ERROR_INVALID_DATA);
	}

	// A valid inherited token means the launcher initiated this process. Later
	// failures exit immediately so a hidden dialog cannot look like a timeout.
	g_showUnauthorizedLaunchMessage = false;
	DWORD parentError = ERROR_SUCCESS;
	if (!HasExpectedParent(log, parentError))
	{
		SecureZeroMemory(token, sizeof(token));
		return FinishAuthorization(log, false, L"parent-validation", parentError);
	}

	wchar_t permitName[128]{};
	wchar_t acknowledgementName[128]{};
	if (!BuildObjectName(permitName, _countof(permitName), PermitNamePrefix, token)
		|| !BuildObjectName(
			acknowledgementName,
			_countof(acknowledgementName),
			AcknowledgementNamePrefix,
			token))
	{
		SecureZeroMemory(token, sizeof(token));
		return FinishAuthorization(log, false, L"object-name", ERROR_INVALID_NAME);
	}

	const HANDLE permit = OpenSemaphoreW(SYNCHRONIZE, FALSE, permitName);
	const DWORD permitError = permit == nullptr ? GetLastError() : ERROR_SUCCESS;
	const HANDLE acknowledgement = OpenEventW(EVENT_MODIFY_STATE, FALSE, acknowledgementName);
	const DWORD acknowledgementError = acknowledgement == nullptr ? GetLastError() : ERROR_SUCCESS;
	SecureZeroMemory(token, sizeof(token));
	if (permit == nullptr || acknowledgement == nullptr)
	{
		if (permit != nullptr)
		{
			CloseHandle(permit);
		}
		if (acknowledgement != nullptr)
		{
			CloseHandle(acknowledgement);
		}
		return FinishAuthorization(
			log,
			false,
			permit == nullptr ? L"open-permit" : L"open-acknowledgement",
			permit == nullptr ? permitError : acknowledgementError);
	}

	const DWORD permitWait = WaitForSingleObject(permit, 0);
	const DWORD permitWaitError = permitWait == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
	if (permitWait != WAIT_OBJECT_0)
	{
		CloseHandle(acknowledgement);
		CloseHandle(permit);
		return FinishAuthorization(
			log,
			false,
			L"consume-permit",
			permitWait == WAIT_FAILED ? permitWaitError : ERROR_NOT_READY);
	}

	const bool acknowledged = SetEvent(acknowledgement) != FALSE;
	const DWORD acknowledgementSignalError = acknowledged ? ERROR_SUCCESS : GetLastError();
	CloseHandle(acknowledgement);
	CloseHandle(permit);
	return FinishAuthorization(
		log,
		acknowledged,
		acknowledged ? L"acknowledged" : L"signal-acknowledgement",
		acknowledgementSignalError);
}

bool LauncherGate::ShouldShowUnauthorizedLaunchMessage()
{
	return g_showUnauthorizedLaunchMessage;
}

void LauncherGate::ShowUnauthorizedLaunchMessage()
{
	MessageBoxW(
		nullptr,
		L"\u8BF7\u8FD0\u884C\u201C\u9010\u68A6\u542F\u52A8\u5668\uFF08ZhuMengLauncher.exe\uFF09\u201D\u542F\u52A8\u6E38\u620F\u3002",
		L"\u65E0\u6CD5\u542F\u52A8\u6E38\u620F",
		MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}
