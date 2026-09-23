#include "../ezorsia/LauncherParentPolicy.h"
#include <cstdio>

int main()
{
    const wchar_t* expected = L"C:\\Games\\BeiDou\\ZhuMengLauncher.exe";
    struct Case { const wchar_t* path; bool allowed; };
    const Case cases[] = {
        {L"C:\\Games\\BeiDou\\ZhuMengLauncher.exe", true},
        {L"C:\\Games\\BeiDou\\ZhuMengLauncher-Dev.exe", true},
        {L"c:\\games\\beidou\\ZHUMENGLAUNCHER-DEV.EXE", true},
        {L"C:\\Other\\ZhuMengLauncher.exe", false},
        {L"C:\\Other\\ZhuMengLauncher-Dev.exe", false},
        {L"C:\\Games\\BeiDou2\\ZhuMengLauncher-Dev.exe", false},
        {L"C:\\Games\\BeiDou\\sub\\ZhuMengLauncher-Dev.exe", false},
        {L"C:\\Games\\BeiDou\\Other.exe", false},
        {L"C:\\Games\\BeiDou\\ZhuMengLauncher-Dev.exe.bak", false},
        {L"C:\\Games\\BeiDou\\ZhuMengLauncher-Dev-other.exe", false},
        {L"ZhuMengLauncher-Dev.exe", false},
        {L"", false},
        {nullptr, false}
    };
    for (const auto& test : cases) {
        if (LauncherParentPolicy::IsAllowed(test.path, expected) != test.allowed) {
            std::fputs("FAIL launcher parent path policy\n", stderr);
            return 1;
        }
    }
    if (LauncherParentPolicy::IsAllowed(expected, nullptr)) return 1;
    if (!LauncherParentPolicy::IsAllowed(L"C:\\游戏\\ZhuMengLauncher-Dev.exe",
        L"C:\\游戏\\ZhuMengLauncher.exe")) return 1;
    std::puts("PASS launcher parent policy: release/dev, case, Unicode, wrong directory/name");
    return 0;
}
