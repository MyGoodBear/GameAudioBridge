# Game Audio Bridge 1.0 UI/INI update (experimental)

This source package adds a standalone Windows GUI executable named **GameAudioBridge.exe**. The user does not need to launch PowerShell or `Launch-Bridge.ps1`.

## GUI

Run `GameAudioBridge.exe`. The window has two independently scrollable lists: **Перехват выключен** and **Перехват включён**. Add one or more EXE files or scan a folder; architecture is detected from each PE header and displayed as `[x86]` or `[x64]`. Move entries between lists to control which profiles are enabled. Double-clicking a row moves that game to the opposite list and toggles interception. Select one enabled profile and press **Запустить выбранную игру** to run it using the matching x86/x64 launcher and hook DLL.

## Portable INI

`GameAudioBridge.ini` is stored in the **same folder as `GameAudioBridge.exe`**. It contains game profiles only; global output-mode settings are not written to it. The output-mode choice applies only to the current GUI session. The installer uses a per-user folder (`%LOCALAPPDATA%\Programs\Game Audio Bridge`) so the GUI can update the INI without administrator rights.

Profiles use ordinary INI sections, for example:

```ini
[Far Cry]
RegPath=HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{D6DBDC2A-E72C-4284-B6AD-6B3B61B4DABC}\InstallLocation
SubDir=Bin32
Preset=1
Enabled=FALSE

[Steam Alan Wake (disk D)]
RegPath=
GamePath=D:\Games\Steam\steamapps\common\Alan Wake
SubDir=
RootDirInstallOption=FALSE
Buffers=4
Duration=25
MaxVoiceCount=128
DisableDirectMusic=FALSE
Enabled=FALSE
```

- `RegPath` is a registry value path ending with the value name, such as `...\\InstallLocation`. The program reads that value to resolve the install folder.
- `GamePath` is used when the game is installed at a manually specified path. `SubDir` is appended to the install folder when present.
- `ExePath` is optional and is written automatically when adding a specific EXE; it removes ambiguity when a game folder contains multiple executables.
- `Enabled=TRUE` puts the profile in the enabled list; `FALSE` puts it in the disabled list. Profiles without `Enabled` default to disabled.
- Unknown profile keys are preserved when the INI is saved, so settings can be edited manually and moved between PCs.

**Important limitation about ALchemy-style options:** `Buffers`, `Duration`, `MaxVoiceCount`, `Preset`, `RootDirInstallOption`, and `DisableDirectMusic` are stored and preserved in the INI, but this experimental WASAPI hook does not currently apply those ALchemy-specific settings to the audio engine. They are not yet active processing controls.


## Output modes — current implementation status

The GUI lists Hardware OpenAL Creative, ASIO, and WASAPI Exclusive. In this source update, only the existing experimental Creative OpenAL path is implemented. ASIO and WASAPI Exclusive are deliberately blocked at launch with an explicit message; selecting them does **not** route audio through those backends yet. A real ASIO backend requires an ASIO host implementation and driver enumeration/callback handling; WASAPI Exclusive needs its own exclusive-mode output engine and format negotiation. These are audio-engine changes, not just GUI/INI settings. The Creative OpenAL probe also does not prove hardware acceleration for every game path.

## Build using GitHub Actions

1. Upload all files in this archive to your repository, preserving the `.github` folder.
2. Open **Actions** → **Build Windows installer** → **Run workflow**.
3. Download the `GameAudioBridge-Setup` artifact after the workflow succeeds.

The workflow compiles `GameAudioBridge.exe` using the .NET Framework C# compiler and builds native x86 and x64 hook/launcher binaries with CMake and Visual Studio.

## Local build

Requirements: Visual Studio 2022 with Desktop development with C++, CMake, and .NET Framework 4.x compiler.

```powershell
# Compile the GUI EXE (from project root in a Developer PowerShell)
New-Item -ItemType Directory -Force dist | Out-Null
$framework = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319"
& "$framework\csc.exe" /nologo /target:winexe /platform:anycpu /out:dist\GameAudioBridge.exe /reference:"$framework\System.Windows.Forms.dll" /reference:"$framework\System.Drawing.dll" src\GameAudioBridgeGui.cs

# Native x64
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release

# Native x86
cmake -S . -B build-x86 -A Win32
cmake --build build-x86 --config Release
```

Stage the resulting DLLs and launchers into `dist\x64` and `dist\x86`, copy `config\GameAudioBridge.ini` and `README.md` to `dist\GameAudioBridge.ini` and `dist\README.txt`, then run Inno Setup 6 on `installer\GameAudioBridge.iss`.


## Режим аппаратного OpenAL Creative (экспериментальный)

Режим Creative OpenAL использует системный `OpenAL32.dll` соответствующей разрядности и проверяет идентификаторы vendor/renderer. Эта проверка подтверждает только обнаружение Creative OpenAL; она не доказывает аппаратное ускорение каждого игрового пути.

В этой версии выбор режима вывода применяется только в текущем сеансе GUI и не записывается в `GameAudioBridge.ini`. INI хранит только профили игр и их состояние перехвата.

**Статус остальных выходов:** ASIO Steinberg и WASAPI Exclusive в этом исходном коде не реализованы. Интерфейс блокирует запуск игры при выборе этих режимов, чтобы не создавать ложного впечатления, что они работают. Скрипт `tools/Fetch-ASIO-SDK.ps1` подготавливает официальный SDK под GPLv3, но не подключает его к аудиодвижку.

## Testing the OpenAL path

After launching a game from the enabled list, inspect `%TEMP%\GameAudioBridge.log`. Useful messages include:

- `OpenAL probe: vendor=...; renderer=...; device=...` — identifies the system OpenAL implementation and selected default device. For Creative hardware OpenAL, verify that the vendor/renderer identify Creative / the X-Fi device rather than OpenAL Soft.
- `FMOD_System_Create succeeded; requested native OpenAL output ...` — the legacy FMOD Ex hook ran.
- `FMOD System_SetOutput(OpenAL) returned 0.` — FMOD accepted the OpenAL backend request.
- `WASAPI IMMDeviceEnumerator created` / `IAudioClient initialized` — the separate WASAPI bridge path was reached.

Delete the old log before each test so entries are easy to distinguish. If the game crashes only when launched through Game Audio Bridge, disable the profile and launch the game normally to compare; this hook is experimental and should not be used with anti-cheat/protected processes.

## Experimental limitations

- This is not a production-ready replacement for Creative ALchemy. Compatibility with individual games and hardware-accelerated OpenAL has not been confirmed.
- The WASAPI bridge still handles only supported PCM/float formats; unsupported formats may continue through the original WASAPI path. For legacy FMOD Ex (`fmodex.dll`) the hook now attempts to select FMOD’s native OpenAL output (enum value used by FMOD Ex 4.x) before mixer initialization. The hook preloads the system OpenAL32.dll and logs the OpenAL vendor/renderer/device probe. This is experimental and can fail if the game uses another FMOD ABI, does not import the hooked functions, or the OpenAL driver is unavailable.
- Do not use with anti-cheat or protected processes. Process injection may be blocked.
- x86 and x64 executable architectures are detected from the PE header; ARM64 is identified as unsupported.
