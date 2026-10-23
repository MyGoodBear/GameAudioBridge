# Game Audio Bridge 0.5 — experimental Windows prototype

This is **not yet a verified production application**. The Windows build workflow in `.github/workflows/windows-build.yml` can build x64/x86 binaries and package `GameAudioBridge-Setup.exe` on a Windows GitHub Actions runner. This Linux environment does not have the Windows SDK/MSVC or Inno Setup, so no compiled installer is included in this source archive.

## Signal duplication behavior

The hook attempts to mute the original WASAPI render buffer **only after** conversion succeeds and the converted block has been successfully enqueued for OpenAL output. Unsupported formats, OpenAL setup failures and queue overflow are left on the original WASAPI path. This avoids intentional double playback for successfully redirected buffers. It is not a guarantee of seamless fallback if OpenAL fails after the block is queued or if a game bypasses the hook.

## Formats

The source attempts to accept PCM 8/16/24/32-bit and IEEE float32/float64, including supported `WAVEFORMATEXTENSIBLE` formats. It converts samples to OpenAL float32 where available or PCM16. Float64 precision is not preserved at output. Multichannel formats depend on the installed OpenAL implementation advertising the needed multichannel format support; otherwise the source attempts a speaker-mask-aware stereo downmix.

## Build locally (Windows 10/11)

Install Visual Studio 2022 with **Desktop development with C++**, CMake, and Inno Setup 6. From PowerShell in this directory:

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release
cmake -S . -B build-x86 -A Win32
cmake --build build-x86 --config Release
```

To build the installer, stage the outputs under `dist\x64` and `dist\x86` as shown in the GitHub Actions workflow, then run:

```powershell
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" installer\GameAudioBridge.iss
```

The output is `Output\GameAudioBridge-Setup.exe`.

## Use

After installing, run `Launch-Bridge.ps1` and click the button to browse for the game/application EXE. The script detects x86/x64 from the EXE header and chooses the matching launcher and DLL. The launcher creates the process suspended, loads the hook DLL, and only then resumes the application, so you do not need to look up a PID or race the game startup. This launches the selected EXE directly; launchers that start a separate child process may require selecting the actual game EXE instead. Do not use with anti-cheat/protected or system processes. Check `%TEMP%\GameAudioBridge.log`.

## Known limitations

- Not universal/system-wide; it hooks a limited WASAPI COM path in a selected process.
- Some games use other APIs, create audio clients before injection, or bypass the hook.
- OpenAL32 availability does not prove Creative hardware DSP acceleration.
- Actual output format and multichannel support depend on the Creative runtime/driver.
- Injection may fail or destabilize a process. Test only with non-critical apps.
