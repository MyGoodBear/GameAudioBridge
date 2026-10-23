# Game Audio Bridge 1.2-dev — ASIO shared-buffer bridge

- Added a named shared-memory ring (`Local\\GameAudioBridge_ASIO_Ring_v1`) shared by the injected x86/x64 WASAPI hook and the x64 ASIO host.
- Supported game WASAPI render buffers are converted sample-by-sample to IEEE-754 `double` (float64) and published to the ring; the ASIO callback reads the frames and applies the configured source-to-hardware-output matrix.
- When the ring accepts a buffer, the original WASAPI buffer is silenced to avoid doubled audio. If the ring is full/busy, the source buffer is left unchanged and a diagnostic is logged.
- ASIO host currently opens at 48 kHz; streams at other sample rates are passed through unchanged. No sample-rate converter is included yet.
- The ASIO driver remains open after closing the routing dialog so the user can launch a game; closing the main GUI releases it.
- WASAPI Exclusive remains unimplemented.

## Verification status

This source package has not been compiled or runtime-tested on Windows in this environment. Build with Visual Studio 2022, CMake 3.21+, and the Steinberg ASIO SDK; test first with a non-critical application and inspect `%TEMP%\\GameAudioBridge.log`. Current bridge targets the intercepted WASAPI render path; it does not make unsupported proprietary game audio APIs universally interceptable.

## Дополнение 1.4-dev — подробный отчёт OpenAL
- Кнопка проверки теперь проверяет отдельно диагностический процесс x86 и x64 (на x64 Windows).
- В отчёт включены путь к EXE, код завершения, полный стандартный вывод и ошибки, vendor/renderer/device/version и итог для каждой разрядности.
- Добавлено окно отчёта с возможностью сохранить TXT. Проверка не утверждает аппаратное ускорение, если оно отдельно не измерено.
- Исходный код не был скомпилирован или запущен в Windows в текущей среде; требуется сборка и проверка на целевой системе.


## ASIO sample-rate handling (development implementation)

- ASIO rate selector: Auto, 44.1, 48, 88.2 and 96 kHz. Auto tries supported rates in order 48, 44.1, 96, 88.2 kHz; it does not claim every driver supports every rate.
- Incoming WASAPI streams report source sample rate and bit depth. If the source rate differs from the active ASIO rate, the GUI shows a red warning and suggests a supported rate; 22.05 kHz prioritizes 44.1 kHz and then 88.2 kHz.
- Optional rate conversion: disabled means mismatched-rate streams are not redirected. Enabled means source samples are converted to the active ASIO rate in float64 before routing, using a 32-tap Blackman-windowed sinc kernel. Input bit depth is decoded to float64 and then encoded in the ASIO driver native output format; equal input/output bit depth is not required.
- Limitations: the current sinc converter works per WASAPI buffer and clamps its edge taps, so buffer-boundary artifacts are possible; it is not yet a fully stateful streaming SRC. Auto selects a supported startup rate before the game source format is known; after a mismatch, the UI suggests a better rate but does not silently restart the ASIO driver. Treat this as experimental until built and tested on Windows with the target Creative driver.
