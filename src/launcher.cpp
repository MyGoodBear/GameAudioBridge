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
    WaitForSingleObject(th, INFINITE);
    DWORD code = 0; GetExitCodeThread(th, &code);
    CloseHandle(th); VirtualFreeEx(proc, remote, 0, MEM_RELEASE); CloseHandle(proc);
    if (!code) { std::wcerr << L"LoadLibraryW failed in target process. Check DLL bitness and dependencies.\n"; return false; }
    return true;
}
static bool LaunchSuspendedAndInject(const std::wstring& exe, const std::wstring& dll) {
    std::wstring fullExe = FullPath(exe);
    std::wstring fullDll = FullPath(dll);
    if (!std::filesystem::exists(fullExe)) { std::wcerr << L"Executable not found: " << fullExe << L"\n"; return false; }
    if (!std::filesystem::exists(fullDll)) { std::wcerr << L"DLL not found: " << fullDll << L"\n"; return false; }

    // Create the target suspended so its audio client cannot be created before the hook is loaded.
    std::wstring cmd = L"\"" + fullExe + L"\"";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    std::filesystem::path exePath(fullExe);
    std::wstring cwd = exePath.parent_path().wstring();
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(fullExe.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
        std::wcerr << L"CreateProcess failed: " << GetLastError() << L"\n";
        return false;
    }

    bool ok = Inject(pi.dwProcessId, fullDll);
    if (!ok) {
        std::wcerr << L"Hook injection failed; the target will be closed rather than run without interception.\n";
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return false;
    }
    if (ResumeThread(pi.hThread) == (DWORD)-1) {
        std::wcerr << L"ResumeThread failed: " << GetLastError() << L"\n";
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return false;
    }
    std::wcout << L"Started with hook: " << fullExe << L" (PID " << pi.dwProcessId << L")\n";
    std::wcout << L"Diagnostics: %TEMP%\\GameAudioBridge.log\n";
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}

int wmain(int argc, wchar_t** argv) {
    std::wcout << L"Game Audio Bridge 0.5 — experimental WASAPI/OpenAL bridge\n";
    if (argc == 4 && std::wstring(argv[1]) == L"--launch") {
        return LaunchSuspendedAndInject(argv[2], argv[3]) ? 0 : 1;
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--attach") {
        DWORD pid = wcstoul(argv[2], nullptr, 10);
        if (!pid) { std::wcerr << L"Invalid PID\n"; return 2; }
        auto dll = FullPath(argv[3]);
        if (!std::filesystem::exists(dll)) { std::wcerr << L"DLL not found: " << dll << L"\n"; return 2; }
        if (!Inject(pid, dll)) return 1;
        std::wcout << L"DLL loaded into PID " << pid << L". Existing audio clients may not be intercepted.\n";
        return 0;
    }
    std::wcout << L"Usage:\n"
               << L"  GameAudioBridgeLauncher.exe --launch <full-path-to-exe> <full-path-to-hook.dll>\n"
               << L"  GameAudioBridgeLauncher.exe --attach <PID> <full-path-to-hook.dll>\n\n"
               << L"Recommended: use Launch-Bridge.ps1 to select an EXE; it chooses x86/x64 automatically.\n";
    return 2;
}
