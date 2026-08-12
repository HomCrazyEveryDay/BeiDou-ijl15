#include "stdafx.h"
#include "LauncherGate.h"
#include <TlHelp32.h>
#include <cwchar>

namespace
{
	constexpr wchar_t TokenEnvironmentVariable[] = L"ZHUMENG_LAUNCH_TOKEN";
	constexpr wchar_t PermitNamePrefix[] = L"Local\\ZhuMengLaunchPermit-";
	constexpr wchar_t AcknowledgementNamePrefix[] = L"Local\\ZhuMengLaunchAck-";
	constexpr wchar_t LauncherExecutable[] = L"ZhuMengLauncher.exe";
	constexpr size_t TokenLength = 64;

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

	bool TryGetParentProcessId(DWORD& parentProcessId)
	{
		const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
		{
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

		CloseHandle(snapshot);
		return found;
	}

	bool TryGetParentProcessPath(wchar_t* path, DWORD capacity)
	{
		DWORD parentProcessId = 0;
		if (!TryGetParentProcessId(parentProcessId))
		{
			return false;
		}

		const HANDLE parent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentProcessId);
		if (parent == nullptr)
		{
			return false;
		}

		DWORD length = capacity;
		const BOOL succeeded = QueryFullProcessImageNameW(parent, 0, path, &length);
		CloseHandle(parent);
		return succeeded && length > 0 && length < capacity;
	}

	bool TryGetExpectedLauncherPath(wchar_t* path, DWORD capacity)
	{
		const DWORD length = GetModuleFileNameW(nullptr, path, capacity);
		if (length == 0 || length >= capacity)
		{
			return false;
		}

		wchar_t* separator = wcsrchr(path, L'\\');
		if (separator == nullptr)
		{
			return false;
		}

		const size_t remaining = capacity - static_cast<size_t>((separator + 1) - path);
		return wcscpy_s(separator + 1, remaining, LauncherExecutable) == 0;
	}

	bool HasExpectedParent()
	{
		wchar_t parentPath[32768]{};
		wchar_t expectedPath[32768]{};
		if (!TryGetParentProcessPath(parentPath, _countof(parentPath))
			|| !TryGetExpectedLauncherPath(expectedPath, _countof(expectedPath)))
		{
			return false;
		}

		return CompareStringOrdinal(parentPath, -1, expectedPath, -1, TRUE) == CSTR_EQUAL;
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
	wchar_t token[TokenLength + 1]{};
	const DWORD tokenLength = GetEnvironmentVariableW(
		TokenEnvironmentVariable,
		token,
		_countof(token));
	SetEnvironmentVariableW(TokenEnvironmentVariable, nullptr);
	if (tokenLength != TokenLength || !IsValidToken(token) || !HasExpectedParent())
	{
		SecureZeroMemory(token, sizeof(token));
		return false;
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
		return false;
	}

	SecureZeroMemory(token, sizeof(token));
	const HANDLE permit = OpenSemaphoreW(SYNCHRONIZE, FALSE, permitName);
	const HANDLE acknowledgement = OpenEventW(EVENT_MODIFY_STATE, FALSE, acknowledgementName);
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
		return false;
	}

	const bool authorized = WaitForSingleObject(permit, 0) == WAIT_OBJECT_0
		&& SetEvent(acknowledgement);
	CloseHandle(acknowledgement);
	CloseHandle(permit);
	return authorized;
}

void LauncherGate::ShowUnauthorizedLaunchMessage()
{
	wchar_t launcherPath[32768]{};
	const wchar_t* launcherFileName = LauncherExecutable;
	if (TryGetExpectedLauncherPath(launcherPath, _countof(launcherPath)))
	{
		const wchar_t* separator = wcsrchr(launcherPath, L'\\');
		if (separator != nullptr && separator[1] != L'\0')
		{
			launcherFileName = separator + 1;
		}
	}

	wchar_t message[512]{};
	wcscpy_s(message, L"\u8BF7\u8FD0\u884C\u201C");
	wcscat_s(message, launcherFileName);
	wcscat_s(message, L"\u201D\u542F\u52A8\u6E38\u620F\u3002");
	FatalAppExitW(0, message);
}
