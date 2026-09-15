#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <strsafe.h>
#include <algorithm>
#include <string>
#include <vector>
#include <cwchar>

void Record(const wchar_t* path, DWORD pid, const char* event, DWORD code, DWORD error = 0) {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char line[512]{};
    StringCchPrintfA(line, ARRAYSIZE(line),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ targetPid=%lu monitorPid=%lu event=%s exitCode=%08lX win32Error=%lu\r\n",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,
        pid,GetCurrentProcessId(),event,code,error);
    HANDLE file = CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return;
    DWORD written=0; WriteFile(file,line,static_cast<DWORD>(strlen(line)),&written,nullptr);
    FlushFileBuffers(file); CloseHandle(file);
}

// Only our crash artifacts. No recursion, no following directory links, no
// deletion of active files; preserve newest 10 dumps within 256 MiB.
void PruneDumps(const std::wstring& directory, const wchar_t* log, DWORD pid) {
    struct Entry { std::wstring name; ULONGLONG size, time; };
    std::vector<Entry> files;
    WIN32_FIND_DATAW data{};
    HANDLE search=FindFirstFileW((directory+L"\\ijl15-crash-*.dmp").c_str(),&data);
    if(search==INVALID_HANDLE_VALUE) return;
    do {
        if(data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        files.push_back({data.cFileName,(static_cast<ULONGLONG>(data.nFileSizeHigh)<<32)|data.nFileSizeLow,
            (static_cast<ULONGLONG>(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime});
    } while(FindNextFileW(search,&data));
    FindClose(search);
    std::sort(files.begin(),files.end(),[](const Entry&a,const Entry&b){return a.time>b.time;});
    ULONGLONG kept=0; size_t count=0;
    for(const auto& entry:files) {
        if(count<10 && kept+entry.size<=256ULL*1024*1024) {++count;kept+=entry.size;continue;}
        const std::wstring path=directory+L"\\"+entry.name;
        HANDLE file=CreateFileW(path.c_str(),DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE) continue;
        FILE_DISPOSITION_INFO remove{TRUE};
        if(!SetFileInformationByHandle(file,FileDispositionInfo,&remove,sizeof(remove)))
            Record(log,pid,"retention_delete_failed",0,GetLastError());
        CloseHandle(file);
    }
}

int wmain(int argc,wchar_t** argv) {
    if(argc!=4) return 2;
    wchar_t* end=nullptr;
    const auto value=_wcstoui64(argv[1],&end,10); if(!end||*end||!value) return 2;
    HANDLE process=reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(value));
    const DWORD pid=wcstoul(argv[2],&end,10); if(!end||*end||!pid||GetProcessId(process)!=pid) return 3;
    std::wstring path=argv[3]; const auto slash=path.find_last_of(L"\\/"); if(slash==std::wstring::npos) return 2;
    const std::wstring directory=path.substr(0,slash);
    Record(argv[3],pid,"monitor_ready",0);
    PruneDumps(directory,argv[3],pid);
    const DWORD wait=WaitForSingleObject(process,INFINITE); // Kernel wait: no polling.
    DWORD code=0;
    if(wait==WAIT_OBJECT_0 && GetExitCodeProcess(process,&code)) Record(argv[3],pid,"process_exited",code);
    else Record(argv[3],pid,"monitor_wait_failed",0,GetLastError());
    CloseHandle(process);
    PruneDumps(directory,argv[3],pid);
    return wait==WAIT_OBJECT_0 ? 0 : 4;
}
