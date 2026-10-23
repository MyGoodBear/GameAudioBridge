# Game Audio Bridge 0.6 (experimental)

Experimental Windows audio interception prototype. It attempts to inject a hook DLL into a newly launched game process and redirect supported WASAPI PCM16 mono/stereo streams through OpenAL. This is not a production-ready replacement for Creative ALchemy; hardware acceleration and compatibility with a particular game or sound card are not guaranteed.

## Game manager

Run `Launch-Bridge.ps1`. The window has two lists:
- **Перехват выключен** — detected games not currently enabled.
- **Перехват включён** — games you have enabled for launching through the bridge.

Use **Добавить EXE…** to choose one or multiple executables, or **Добавить папку…** to recursively scan a selected folder for EXE files. The tool reads the PE header and labels each supported executable `[x86]` or `[x64]`; unsupported architectures are skipped. Move games between the lists with the arrow buttons. Select one game in the enabled list and click **Запустить выбранную игру с перехватом**.

The list is stored in `GameAudioBridge.ini` in the same folder as `Launch-Bridge.ps1`. This makes the configuration portable: copy the INI together with the application folder to another PC. Paths are Base64-encoded within the INI so unusual characters in Windows paths are preserved. The file is plain text and can be backed up or removed to reset the lists. Ensure the application folder is writable by your Windows user.

## Build (Windows 10/11)

### Easiest: GitHub Actions
1. Create/open your GitHub repository and upload all files from this source ZIP, preserving folder structure (including `.github/workflows`).
2. Open **Actions** → **Build Windows installer** → **Run workflow**.
3. When the run succeeds, open it and download the `GameAudioBridge-Setup` artifact.

### Local compile
1. Install Visual Studio 2022 with **Desktop development with C++**, CMake, and Inno Setup 6.
2. Open **x64 Native Tools Command Prompt for VS 2022** in the project folder.
3. Build x64: `cmake -S . -B build-x64 -A x64` then `cmake --build build-x64 --config Release`.
4. For x86, open **x86 Native Tools Command Prompt for VS 2022** and run `cmake -S . -B build-x86 -A Win32` then `cmake --build build-x86 --config Release`.
5. To create the installer, use the workflow's staging commands to copy both builds into `dist\x64` and `dist\x86`, then run `ISCC.exe installer\GameAudioBridge.iss`.

The build has not been validated on every target PC. The GUI is a PowerShell Windows Forms script; the native launcher and hook DLL are compiled by CMake.

## Important limitations

- Experimental software. It does not guarantee that every game uses the intercepted WASAPI path.
- The current hook supports only limited PCM16 mono/stereo cases; unsupported formats may remain on the original WASAPI path.
- Do not use with anti-cheat or protected processes. Injection may be blocked by security software.
- The x86/x64 bitness is detected from the selected executable's PE header. ARM64 is identified but not supported by the included launchers.
- The project does not guarantee that the Creative X-Fi hardware OpenAL driver is used or hardware-accelerated.
