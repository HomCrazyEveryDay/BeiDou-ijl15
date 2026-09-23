#pragma once
#include <Windows.h>
#include <cwchar>

namespace LauncherParentPolicy
{
inline bool IsAllowed(const wchar_t* parentPath, const wchar_t* expectedReleasePath)
{
    if (!parentPath || !expectedReleasePath) return false;
    const wchar_t* parentName = wcsrchr(parentPath, L'\\');
    const wchar_t* expectedName = wcsrchr(expectedReleasePath, L'\\');
    if (!parentName || !expectedName) return false;
    const size_t directoryLength = expectedName - expectedReleasePath + 1;
    if (directoryLength > 32767 || static_cast<size_t>(parentName - parentPath + 1) != directoryLength)
        return false;
    if (CompareStringOrdinal(parentPath, static_cast<int>(directoryLength),
        expectedReleasePath, static_cast<int>(directoryLength), TRUE) != CSTR_EQUAL)
        return false;
    return CompareStringOrdinal(parentName + 1, -1, L"ZhuMengLauncher.exe", -1, TRUE) == CSTR_EQUAL
        || CompareStringOrdinal(parentName + 1, -1, L"ZhuMengLauncher-Dev.exe", -1, TRUE) == CSTR_EQUAL;
}
}
