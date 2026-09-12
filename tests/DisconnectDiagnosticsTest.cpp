#include "../ezorsia/stdafx.h"
#include <winsock2.h>
#include "../ezorsia/DisconnectDiagnostics.h"
#include "../ezorsia/DisconnectClassification.h"
#include "../ezorsia/ClientLog.h"
#include <fstream>
#include <iterator>
#include <string>
#include <cstdio>

static DWORD g_faultEip = 0;
static DWORD g_faultRead = 0;
static DWORD g_faultReturn = 0;
static LONG SaveFault(EXCEPTION_POINTERS* info) {
    g_faultEip = info->ContextRecord->Eip;
    g_faultRead = static_cast<DWORD>(info->ExceptionRecord->ExceptionInformation[1]);
    return EXCEPTION_EXECUTE_HANDLER;
}
__declspec(naked) static void FaultReadFour() {
    __asm { mov eax, [esp] }
    __asm { mov g_faultReturn, eax }
    __asm { mov eax, 4 }
    __asm { mov eax, [eax] }
    __asm { ret }
}
__declspec(noinline) static bool CaptureRealFault() {
    __try { FaultReadFour(); }
    __except (SaveFault(GetExceptionInformation())) { return true; }
    return false;
}

static bool ExceptionStillPropagates() {
    __try { RaiseException(0xE06D7363, 0, 0, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
int main() {
    using namespace DisconnectDiagnostics;
    const std::uintptr_t keyboard[] = {0x94872,0x62467F,0x6068BB,0x607529,0x12997F};
    const std::uintptr_t menu[] = {0x94872,0x62467F,0x6068BB,0x4D42B0,0xC0A63};
    const std::uintptr_t network[] = {0x94872,0x62467F,0x6068BB,0x607529,0x5FEB20};
    const std::uintptr_t incomplete[] = {0x94872,0x62467F,0x6068BB};
    const std::uintptr_t earlyCleanup[] = {0x5F69DD,0x5F5219,0x5F1CF1};
    const std::uintptr_t cleanup[] = {0x94872,0x5F5219,0x5F1CF1};
    if (ClassifyClosePath(keyboard,5) != ClosePath::ConfirmedLogout
        || ClassifyClosePath(menu,5) != ClosePath::ConfirmedLogout
        || ClassifyClosePath(network,5) != ClosePath::Unknown
        || ClassifyClosePath(incomplete,3) != ClosePath::Unknown
        || ClassifyClosePath(earlyCleanup,3) != ClosePath::ProcessCleanup
        || ClassifyClosePath(cleanup,3) != ClosePath::ProcessCleanup) return 16;
    ClientLog::Initialize();
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) return 1;
    DisconnectDiagnostics::Install(true);
    DisconnectDiagnostics::Packet(false, 0x14, 6);
    for (int i = 0; i < 50; ++i) DisconnectDiagnostics::Packet(true, static_cast<unsigned short>(0x200 + i), 12);
    char buffer[4]{};
    if (recv(INVALID_SOCKET, buffer, sizeof(buffer), 0) != SOCKET_ERROR || WSAGetLastError() != WSAENOTSOCK) return 2;
    if (!ExceptionStillPropagates()) return 3;
    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET || closesocket(socketHandle)) return 4;
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int addressSize = sizeof(address);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), addressSize)
        || listen(listener, 1) || getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressSize)) return 6;
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connect(client, reinterpret_cast<sockaddr*>(&address), addressSize)) return 7;
    SOCKET peer = accept(listener, nullptr, nullptr);
    if (send(peer, "ok", 2, 0) != 2 || recv(client, buffer, 2, MSG_WAITALL) != 2
        || buffer[0] != 'o' || buffer[1] != 'k') return 8;
    closesocket(peer);
    if (recv(client, buffer, 2, 0) != 0) return 9;
    closesocket(client);
    closesocket(listener);
    DisconnectDiagnostics::ExpectChannelClose();
    SOCKET normalChange = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (closesocket(normalChange)) return 12;
    // A network error cancels the expected-close classification.
    DisconnectDiagnostics::ExpectChannelClose();
    if (recv(INVALID_SOCKET, buffer, sizeof(buffer), 0) != SOCKET_ERROR) return 13;
    SOCKET failedChange = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (closesocket(failedChange)) return 14;
    if (closesocket(INVALID_SOCKET) != SOCKET_ERROR || WSAGetLastError() != WSAENOTSOCK) return 15;
    // Exercise both former quotas, after the former 8 MiB lifecycle file cap.
    const std::string padding(2000, 'x');
    for (int i = 0; i < 4300; ++i)
        ClientLog::Append(ClientLog::Component::Lifecycle, "%s", padding.c_str());
    for (int i = 0; i < 25; ++i) {
        if (recv(INVALID_SOCKET, buffer, sizeof(buffer), 0) != SOCKET_ERROR
            || WSAGetLastError() != WSAENOTSOCK || !ExceptionStillPropagates()) return 10;
    }
    if (!CaptureRealFault() || g_faultRead != 4
        || g_faultEip != reinterpret_cast<DWORD>(&FaultReadFour) + 13) return 17;
    ClientLog::Append(ClientLog::Component::Lifecycle, "repeat_test_begin");
    for (int i = 0; i < 100; ++i) {
        if (i == 50) DisconnectDiagnostics::Packet(true, 0x999, 6);
        SetLastError(ERROR_ACCESS_DENIED);
        if (!CaptureRealFault() || GetLastError() != ERROR_ACCESS_DENIED) return 19;
    }
    ClientLog::Append(ClientLog::Component::Lifecycle, "repeat_test_end");
    const std::wstring path = std::wstring(ClientLog::Directory()) + L"/ijl15-lifecycle-" + ClientLog::SessionId() + L".log";
    std::ifstream file(path, std::ios::binary);
    const std::string log((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    char expectedFault[100]{};
    sprintf_s(expectedFault, "code=C0000005 address=%08lX eip=%08lX", g_faultEip, g_faultEip);
    char expectedReturn[40]{};
    sprintf_s(expectedReturn, "+%lX", g_faultReturn - reinterpret_cast<DWORD>(GetModuleHandleW(nullptr)));
    if (log.find(expectedFault) == std::string::npos
        || log.find(expectedReturn) == std::string::npos) return 18;
    auto count = [&](const char* needle) {
        size_t total = 0, pos = 0;
        while ((pos = log.find(needle, pos)) != std::string::npos) { ++total; ++pos; }
        return total;
    };
    if (log.size() <= 8 * 1024 * 1024
        || count("kind=local_socket_close socket=") != 5
        || count("kind=local_socket_close_error socket=") != 1
        || count("kind=recv_error") != 27
        || count("kind=first_chance_exception_not_necessarily_fatal") != 127) return 11;
    if (log.find("recvHook=1 closeHook=1 exceptionObserver=1") == std::string::npos
        || log.find("kind=recv_error") == std::string::npos
        || log.find("kind=local_socket_close") == std::string::npos
        || log.find("kind=peer_eof") == std::string::npos
        || log.find("kind=first_chance_exception_not_necessarily_fatal") == std::string::npos
        || log.find("opcode=0231") == std::string::npos
        || log.find("opcode=0200") != std::string::npos) return 5;
    const auto begin = log.find("repeat_test_begin");
    const auto end = log.find("repeat_test_end");
    if (begin == std::string::npos || end == std::string::npos || end - begin > 100000
        || count("exception_repeat ") < 95 || count("opcode=0999") == 0) return 20;
    printf("100 real exceptions retained: %zu bytes, with refreshed details after a new packet\n", end - begin);
    WSACleanup();
    puts("PASS socket hooks preserve errors, exceptions propagate, packet history is bounded");
    return 0;
}
