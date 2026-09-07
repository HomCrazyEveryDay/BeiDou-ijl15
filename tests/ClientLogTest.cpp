#include "../ezorsia/ClientLog.h"
#include "../ezorsia/CrashReporter.h"
#include "../ezorsia/ClientDiagnostics.h"

#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}

std::string Read(const std::wstring& path)
{
    std::ifstream input(path, std::ios::binary);
    Require(input.good(), "log file is readable");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::array<unsigned char, ClientDiagnostics::kAckPacketSize> AckFor(
    const unsigned char (&identify)[ClientDiagnostics::kIdentifyPacketSize], unsigned long long session)
{
    std::array<unsigned char, ClientDiagnostics::kAckPacketSize> ack{};
    ack[4] = 0x07;
    ack[5] = 0x10;
    std::memcpy(ack.data() + 6, identify + 2, 21);
    for (unsigned int i = 0; i < 16; ++i) ack[27 + i] = static_cast<unsigned char>(i + 1);
    for (unsigned int i = 0; i < 8; ++i) ack[43 + i] = static_cast<unsigned char>(session >> (8 * i));
    return ack;
}

void CheckState(ClientDiagnostics::ConnectionState state, std::uint32_t attempt, const char* connection = "unavailable")
{
    ClientDiagnostics::Snapshot snapshot;
    Require(ClientDiagnostics::TrySnapshot(snapshot), "diagnostic snapshot is available");
    Require(snapshot.connectionState == state && snapshot.diagnosticAttempt == attempt
        && std::strcmp(snapshot.connectionId, connection) == 0, "connection snapshot is coherent");
    Require(std::strcmp(snapshot.clientRunId, ClientDiagnostics::RunId()) == 0, "snapshot retains process run ID");
}

void CheckDiagnostics()
{
    using namespace ClientDiagnostics;
    const std::string run = RunId();
    Require(run.size() == 32 && run.find_first_not_of("0123456789abcdef") == std::string::npos
        && run != std::string(32, '0'), "secure process run ID is nonzero lowercase hex");
    Initialize();
    Require(run == RunId(), "process run ID remains stable after repeated initialization");
    CheckState(ConnectionState::Unbound, 0);
    unsigned char identify[kIdentifyPacketSize]{};
    Require(!BeginConnection(identify, 1), "request is disabled until ACK hook is installed");
    CheckState(ConnectionState::Unavailable, 0);
    SetAckConsumerReady(true);
    Require(BeginConnection(identify, 1), "login connection diagnostic is prepared");
    Require(identify[0] == 2 && identify[1] == 0x10 && identify[2] == 1
        && identify[19] == 1 && identify[20] == 0 && identify[21] == 0 && identify[22] == 0,
        "identify packet has exact opcode, version and little-endian attempt");
    CheckState(ConnectionState::Pending, 1);
    auto ack = AckFor(identify, 9223372036854775807ULL);
    auto bad = ack;
    bad[7] ^= 1;
    Require(HandleIncoming(bad.data(), bad.size()), "wrong-run ACK is consumed");
    CheckState(ConnectionState::Pending, 1);
    bad = ack; bad[23] = 2;
    Require(HandleIncoming(bad.data(), bad.size()), "wrong-attempt ACK is consumed");
    CheckState(ConnectionState::Pending, 1);
    bad = ack; bad[6] = 2;
    Require(HandleIncoming(bad.data(), bad.size()), "wrong-version ACK is consumed");
    CheckState(ConnectionState::Pending, 1);
    bad = ack; std::memset(bad.data() + 27, 0, 16);
    Require(HandleIncoming(bad.data(), bad.size()), "zero-boot ACK is consumed");
    CheckState(ConnectionState::Pending, 1);
    bad = ack; bad[50] |= 0x80;
    Require(HandleIncoming(bad.data(), bad.size()), "negative-session ACK is consumed");
    CheckState(ConnectionState::Pending, 1);
    for (std::size_t size = 6; size < ack.size(); ++size) {
        Require(HandleIncoming(ack.data(), size), "truncated ACK is consumed");
        CheckState(ConnectionState::Pending, 1);
    }
    Require(HandleIncoming(ack.data(), ack.size() + 1), "oversized ACK is consumed without reading payload");
    Require(!HandleIncoming(nullptr, 0) && !HandleIncoming(ack.data(), 5), "non-identifiable packets remain untouched");
    bad = ack; bad[4] = 0x11;
    Require(!HandleIncoming(bad.data(), bad.size()), "normal packets remain untouched");
    SetLastError(ERROR_ACCESS_DENIED);
    Require(HandleIncoming(ack.data(), ack.size()), "valid ACK is consumed");
    Require(GetLastError() == ERROR_ACCESS_DENIED, "ACK processing preserves Win32 last error");
    const char* maximumConnection = "0102030405060708090a0b0c0d0e0f10-9223372036854775807";
    CheckState(ConnectionState::Bound, 1, maximumConnection);
    Require(HandleIncoming(ack.data(), ack.size()), "duplicate ACK is consumed idempotently");
    auto replacement = AckFor(identify, 2);
    Require(HandleIncoming(replacement.data(), replacement.size()), "same-attempt rebinding ACK is consumed");
    CheckState(ConnectionState::Bound, 1, maximumConnection);
    Require(BeginConnection(identify, 0x14), "channel connection diagnostic is prepared");
    CheckState(ConnectionState::Pending, 2);
    Require(run == RunId(), "channel change retains process run ID");
    Require(HandleIncoming(ack.data(), ack.size()), "previous connection ACK is consumed");
    CheckState(ConnectionState::Pending, 2);
    ack = AckFor(identify, 0);
    Require(HandleIncoming(ack.data(), ack.size()), "session zero is valid");
    CheckState(ConnectionState::Bound, 2, "0102030405060708090a0b0c0d0e0f10-0");
    SetAckConsumerReady(false);
    Require(!BeginConnection(identify, 0x14), "request is disabled before ACK hook removal");
    CheckState(ConnectionState::Unavailable, 2);
    SetAckConsumerReady(true);
    Require(BeginConnection(identify, 0x14), "connection can bind after ACK consumer is ready");
    ack = AckFor(identify, 3);
    Require(HandleIncoming(ack.data(), ack.size()), "new ACK is consumed");
    CheckState(ConnectionState::Bound, 3, "0102030405060708090a0b0c0d0e0f10-3");
    for (std::uint32_t attempt = 4; attempt <= 7; ++attempt) {
        Require(BeginConnection(identify, 0x14), "diagnostic attempts increase per connection");
        CheckState(ConnectionState::Pending, attempt);
    }
    ack = AckFor(identify, 7777);
    for (unsigned int i = 0; i < 16; ++i) ack[27 + i] = static_cast<unsigned char>(0x20 + i);
    Require(ack[23] == 7 && ack[24] == 0 && ack[25] == 0 && ack[26] == 0
        && ack[43] == 0x61 && ack[44] == 0x1e && ack[45] == 0 && ack[50] == 0,
        "shared server protocol vector uses little-endian attempt 7 and session 7777");
    Require(HandleIncoming(ack.data(), ack.size()), "shared server protocol ACK is consumed");
    CheckState(ConnectionState::Bound, 7, "202122232425262728292a2b2c2d2e2f-7777");
}
}

int wmain(int argc, wchar_t** argv)
{
    ClientLog::Initialize();
    if (argc > 1 && std::wcscmp(argv[1], L"--run-id") == 0) {
        std::printf("%s\n", ClientDiagnostics::RunId());
        return 0;
    }
    const std::wstring directory = ClientLog::Directory();
    if (argc > 1 && std::wcscmp(argv[1], L"--unwritable") == 0) {
        Require(directory.empty(), "unwritable client logs does not select another directory");
        Require(ClientLog::Open(ClientLog::Component::Trace) == INVALID_HANDLE_VALUE, "unwritable logs disables file creation");
        ClientLog::Append(ClientLog::Component::Trace, "must_not_escape_logs");
        std::printf("PASS unwritable logs stays local\n");
        return 0;
    }
    Require(!directory.empty(), "a writable logs directory exists");
    Require(directory.size() >= 5 && directory.substr(directory.size() - 5) == L"\\logs", "directory ends with logs");
    if (argc > 1) Require(directory == argv[1], "client logs directory selected");
    CheckDiagnostics();
    const std::wstring lifecycle = directory + L"\\ijl15-lifecycle-" + ClientLog::SessionId() + L".log";
    Require(Read(lifecycle).find("diagnostic_binding clientRunId=" + std::string(ClientDiagnostics::RunId())) != std::string::npos,
        "connection binding is logged with verbose tracing disabled");
    const std::wstring path = directory + L"\\ijl15-trace-" + ClientLog::SessionId() + L".log";
    SetLastError(ERROR_ACCESS_DENIED);
    ClientLog::Append(ClientLog::Component::Trace, "first_record");
    Require(GetLastError() == ERROR_ACCESS_DENIED, "logging preserves Win32 last error");
    const std::string prefix = Read(path);

    std::vector<std::thread> writers;
    for (int thread = 0; thread < 4; ++thread) {
        writers.emplace_back([thread] {
            for (int item = 0; item < 100; ++item)
                ClientLog::Append(ClientLog::Component::Trace, "worker=%d item=%03d", thread, item);
        });
    }
    for (auto& writer : writers) writer.join();
    const std::string content = Read(path);
    Require(content.compare(0, prefix.size(), prefix) == 0, "append preserves previously uploaded prefix");
    for (int thread = 0; thread < 4; ++thread) {
        for (int item = 0; item < 100; ++item) {
            char expected[64]{};
            std::snprintf(expected, sizeof(expected), "worker=%d item=%03d\r\n", thread, item);
            const auto position = content.find(expected);
            Require(position != std::string::npos && content.find(expected, position + 1) == std::string::npos,
                "each concurrent record exists exactly once and is complete");
        }
    }
    Require(content.find("format=beidou-client-log-v1") == 0, "versioned header is present");
    Require(content.find("clientRunId=" + std::string(ClientDiagnostics::RunId())) != std::string::npos,
        "component header contains process run ID");
    HANDLE reader = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(reader != INVALID_HANDLE_VALUE, "uploader can keep a shared read handle");
    ClientLog::Append(ClientLog::Component::Trace, "upload_in_progress");
    CloseHandle(reader);
    Require(Read(path).find("upload_in_progress") != std::string::npos, "writing continues during upload");

    CrashReporter::Install(true, "normal", true);
    if (argc > 2 && std::wcscmp(argv[2], L"pending") == 0) {
        unsigned char identify[ClientDiagnostics::kIdentifyPacketSize]{};
        Require(ClientDiagnostics::BeginConnection(identify, 0x14), "new connection is pending before synthetic crash");
    }
    ClientDiagnostics::Snapshot crashSnapshot;
    Require(ClientDiagnostics::TrySnapshot(crashSnapshot), "crash snapshot can be captured");
    const unsigned char privatePayload[] = "test-packet-content-not-for-upload";
    CrashReporter::RecordIncomingPacket(0x11, sizeof(privatePayload), 4, privatePayload, sizeof(privatePayload));
    EXCEPTION_RECORD exception{};
    exception.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    exception.ExceptionAddress = reinterpret_cast<void*>(&wmain);
    CONTEXT context{};
    EXCEPTION_POINTERS pointers{ &exception, &context };
    CrashReporter::CaptureHandledException("test", "synthetic", &pointers);
    WIN32_FIND_DATAW found{};
    const std::wstring pattern = directory + L"\\ijl15-crash-" + ClientLog::SessionId() + L"*.txt";
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    Require(search != INVALID_HANDLE_VALUE, "crash text is written in logs");
    FindClose(search);
    const std::string report = Read(directory + L"\\" + found.cFileName);
    Require(report.find("clientRunId=" + std::string(crashSnapshot.clientRunId) + "\r\n") != std::string::npos
        && report.find("connectionId=" + std::string(crashSnapshot.connectionId) + "\r\n") != std::string::npos
        && report.find("connectionState=" + std::string(ClientDiagnostics::StateName(crashSnapshot.connectionState)) + "\r\n") != std::string::npos
        && report.find("diagnosticAttempt=" + std::to_string(crashSnapshot.diagnosticAttempt) + "\r\n") != std::string::npos,
        "crash text includes process ID and current connection snapshot without stale binding");
    Require(report.find("opcode=0x0011") != std::string::npos, "crash report retains packet metadata");
    Require(report.find(reinterpret_cast<const char*>(privatePayload)) == std::string::npos
        && report.find("payload=") == std::string::npos
        && report.find("stackSnapshot=") == std::string::npos,
        "uploadable crash text excludes packet payloads and raw memory snapshots");
    Require(report.find("dumpWritten=true") != std::string::npos, "existing local minidump generation works");
    search = FindFirstFileW((directory + L"\\ijl15-crash-" + ClientLog::SessionId() + L"*.dmp").c_str(), &found);
    Require(search != INVALID_HANDLE_VALUE, "minidump is written directly in logs");
    FindClose(search);
    Require(GetFileAttributesW((directory + L"\\crash").c_str()) == INVALID_FILE_ATTRIBUTES, "no crash subdirectory is created");

    HANDLE file = ClientLog::Open(ClientLog::Component::Trace);
    const std::string block(65536, 'x');
    for (int i = 0; i < 140; ++i) ClientLog::Write(file, block.c_str());
    LARGE_INTEGER size{};
    Require(GetFileSizeEx(file, &size) && size.QuadPart <= 8 * 1024 * 1024, "session file respects 8 MiB limit");
    CloseHandle(file);
    Require(Read(path).compare(0, prefix.size(), prefix) == 0, "size limiting does not truncate old bytes");
    std::printf("PASS ClientLogTest: diagnostic protocol/state, concurrent append, upload sharing, bounded size, immutable prefix, error state, crash correlation/privacy\n");
    return 0;
}
