#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <vector>
#include <filesystem>

static std::wstring FullPath(const std::wstring& p) {
    std::wstring out(32768, L'\0');
    DWORD n = GetFullPathNameW(p.c_str(), (DWORD)out.size(), out.data(), nullptr);
    if (!n || n >= out.size()) return p;
    out.resize(n); return out;
}
static bool Inject(DWORD pid, const std::wstring& dll) {
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!proc) { std::wcerr << L"OpenProcess failed: " << GetLastError() << L"\n"; return false; }
    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { std::wcerr << L"VirtualAllocEx failed: " << GetLastError() << L"\n"; CloseHandle(proc); return false; }
    if (!WriteProcessMemory(proc, remote, dll.c_str(), bytes, nullptr)) {
        std::wcerr << L"WriteProcessMemory failed: " << GetLastError() << L"\n";
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE); CloseHandle(proc); return false;
    }
    auto load = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, load, remote, 0, nullptr);
    if (!th) {
        std::wcerr << L"CreateRemoteThread failed: " << GetLastError() << L"\n";
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE); CloseHandle(proc); return false;
    }
    WaitForSingleObject(th, 10000);
    DWORD code = 0; GetExitCodeThread(th, &code);
    CloseHandle(th); VirtualFreeEx(proc, remote, 0, MEM_RELEASE); CloseHandle(proc);
    if (!code) { std::wcerr << L"LoadLibraryW failed in target process. Check DLL bitness and dependencies.\n"; return false; }
    return true;
}
int wmain(int argc, wchar_t** argv) {
    std::wcout << L"Game Audio Bridge 0.3 — experimental WASAPI/OpenAL bridge\n";
    if (argc < 3) {
        std::wcout << L"Usage:\n  GameAudioBridgeLauncher.exe <PID> <full-path-to-GameAudioBridgeHook.dll>\n";
        std::wcout << L"Example: GameAudioBridgeLauncher.exe 1234 C:\\GameAudioBridgeHook.dll\n\n";
        std::wcout << L"The launcher injects into one already-running process; it does not install a system-wide hook.\n";
        return 2;
    }
    DWORD pid = wcstoul(argv[1], nullptr, 10);
    if (!pid) { std::wcerr << L"Invalid PID\n"; return 2; }
    auto dll = FullPath(argv[2]);
    if (!std::filesystem::exists(dll)) { std::wcerr << L"DLL not found: " << dll << L"\n"; return 2; }
    if (!Inject(pid, dll)) return 1;
    std::wcout << L"DLL loaded into PID " << pid << L". Check %TEMP%\\GameAudioBridge.log for diagnostics.\n";
    return 0;
}
