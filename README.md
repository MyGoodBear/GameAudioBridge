# Game Audio Bridge 1.2-dev — ASIO shared-memory bridge (experimental)

This source package adds a standalone Windows GUI executable named **GameAudioBridge.exe**. The user does not need to launch PowerShell or `Launch-Bridge.ps1`.

## GUI

Run `GameAudioBridge.exe`. The window has two independently scrollable lists: **Перехват выключен** and **Перехват включён**. Add one or more EXE files or scan a folder; architecture is detected from each PE header and displayed as `[x86]` or `[x64]`. Move entries between lists to control which profiles are enabled. Double-clicking a row moves that game to the opposite list and toggles interception. Select one enabled profile and press **Запустить выбранную игру** to run it using the matching x86/x64 launcher and hook DLL.

## Audio Monitor and game removal

The GUI includes a centered **Аудиомонитор** button near the bottom of the main window. It opens a live viewer for `%TEMP%\GameAudioBridge.log`, refreshes once per second, and can export the visible diagnostic log to a UTF-8 text file. The native hook now logs WASAPI format details (format tag, sample rate, channels, sample width, block alignment, byte rate, extensible valid bits/channel mask/subformat), WASAPI share mode/flags/buffer durations and HRESULT, periodic redirected-buffer/frame counters, queue-full events, and the names of detected audio-related DLL modules. Module detection is diagnostic only: seeing a DLL does not mean that engine is hooked.

Right-click a game in either list and choose **Удалить игру из программы…**. The Yes/No confirmation defaults to **Да** (Enter confirms); choose **Нет** or press Escape to cancel. The existing **Удалить** button uses the same confirmation. Only the profile is removed; game files are not touched.

**Scope of the monitor:** it displays information the hook actually records. It cannot recover details of streams that ended before logging was installed, inspect audio buffers belonging to unhooked APIs, or guarantee every possible format/engine is intercepted. Multiple process IDs may appear in the shared log; each line identifies its PID.

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

The GUI lists Hardware OpenAL Creative, ASIO, and WASAPI Exclusive. This update adds an experimental x64 ASIO host and a shared-memory bridge from supported intercepted WASAPI render buffers to the ASIO callback. The bridge requires 48 kHz input; other sample rates are deliberately passed through instead of being incorrectly played at the wrong rate. WASAPI Exclusive remains unimplemented. The Creative OpenAL probe does not prove hardware acceleration for every game path.

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

**Статус остальных выходов:** ASIO Steinberg имеет экспериментальный x64 host и межпроцессную передачу float64-буферов из WASAPI hook; требуется компиляция и проверка на Windows с реальным драйвером. WASAPI Exclusive пока не реализован.

## Testing the OpenAL path

After launching a game from the enabled list, inspect `%TEMP%\GameAudioBridge.log`. Useful messages include:

- `OpenAL probe: vendor=...; renderer=...; device=...` — identifies the system OpenAL implementation and selected default device. For Creative hardware OpenAL, verify that the vendor/renderer identify Creative / the X-Fi device rather than OpenAL Soft.
- `FMOD_System_Create succeeded; requested native OpenAL output ...` — the legacy FMOD Ex hook ran.
- `FMOD System_SetOutput(OpenAL) returned 0.` — FMOD accepted the OpenAL backend request.
- `WASAPI IMMDeviceEnumerator created` / `IAudioClient initialized` — the separate WASAPI bridge path was reached.

Delete the old log before each test so entries are easy to distinguish. If the game crashes only when launched through Game Audio Bridge, disable the profile and launch the game normally to compare; this hook is experimental and should not be used with anti-cheat/protected processes.

## Experimental limitations

This project does not support every known game engine. The current active interception paths remain limited to selected WASAPI COM imports, legacy FMOD Ex exports and static OpenAL imports; DLL detection is not an implementation of engine-specific hooks. A universal hook would require per-API/per-version adapters and tests. Safe format/device checks and pass-through fallback are intentionally retained to avoid silencing audio or crashing games on unsupported formats.

- This is not a production-ready replacement for Creative ALchemy. Compatibility with individual games and hardware-accelerated OpenAL has not been confirmed.
- The WASAPI bridge still handles only supported PCM/float formats; unsupported formats may continue through the original WASAPI path. For legacy FMOD Ex (`fmodex.dll`) the hook now attempts to select FMOD’s native OpenAL output (enum value used by FMOD Ex 4.x) before mixer initialization. The hook preloads the system OpenAL32.dll and logs the OpenAL vendor/renderer/device probe. This is experimental and can fail if the game uses another FMOD ABI, does not import the hooked functions, or the OpenAL driver is unavailable.
- Do not use with anti-cheat or protected processes. Process injection may be blocked.
- x86 and x64 executable architectures are detected from the PE header; ARM64 is identified as unsupported.

## ASIO shared-memory bridge (1.2-dev)

The x64 ASIO host creates `Local\\GameAudioBridge_ASIO_Ring_v1`. The injected WASAPI hook in either x86 or x64 game processes opens this mapping and forwards supported render buffers as interleaved float64 samples. The ASIO callback routes the 8 logical source channels through the configured matrix to hardware outputs. ASIO host is currently configured for 48 kHz; non-48 kHz game streams are deliberately left on their original path rather than being resampled incorrectly. Keep the ASIO driver open in the routing dialog before launching the game. WASAPI Exclusive is still not implemented.

**Build/test notice:** this is a source package and has not been compiled or hardware-tested on Windows in this environment. Run `build.ps1 -FetchAsioSdk` from a Visual Studio 2022 developer PowerShell with CMake 3.21+ and Git installed. Inspect `%TEMP%\\GameAudioBridge.log` during testing.
## Подробная диагностика Creative OpenAL (x86/x64)

Кнопка «Проверить Creative OpenAL» запускает независимые диагностические процессы x86 и x64 (если ОС поддерживает соответствующую разрядность), показывает полный вывод каждой проверки в отдельном окне и позволяет сохранить отчёт в TXT. Проверяются загрузка системной `OpenAL32.dll`, наличие необходимых экспортов, открытие устройства по умолчанию, создание и активация контекста, а также строки vendor/renderer/device/version и код завершения. Успех этой проверки не доказывает аппаратное ускорение во всех играх или успешный перехват игрового аудио.


## ASIO sample-rate handling (development implementation)

- ASIO rate selector: Auto, 44.1, 48, 88.2 and 96 kHz. Auto tries supported rates in order 48, 44.1, 96, 88.2 kHz; it does not claim every driver supports every rate.
- Incoming WASAPI streams report source sample rate and bit depth. If the source rate differs from the active ASIO rate, the GUI shows a red warning and suggests a supported rate; 22.05 kHz prioritizes 44.1 kHz and then 88.2 kHz.
- Optional rate conversion: disabled means mismatched-rate streams are not redirected. Enabled means source samples are converted to the active ASIO rate in float64 before routing, using a 32-tap Blackman-windowed sinc kernel. Input bit depth is decoded to float64 and then encoded in the ASIO driver native output format; equal input/output bit depth is not required.
- Limitations: the current sinc converter works per WASAPI buffer and clamps its edge taps, so buffer-boundary artifacts are possible; it is not yet a fully stateful streaming SRC. Auto selects a supported startup rate before the game source format is known; after a mismatch, the UI suggests a better rate but does not silently restart the ASIO driver. Treat this as experimental until built and tested on Windows with the target Creative driver.
