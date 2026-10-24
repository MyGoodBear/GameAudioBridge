# Game Audio Bridge 2.3

См. `CHANGELOG_2.3_RU.md` для деталей выборочного преобразования форматов и ограничения OpenAL signed PCM Int32.

﻿# Game Audio Bridge 2.2 — OpenAL Format Bridge

**Important:** this is an experimental source package, not a tested installer. It is not a universal audio interception solution. See `CHANGELOG_2.0_RU.md`, `CHANGELOG_1.9_RU.md`, `CHANGELOG_1.8_RU.md`, `CHANGELOG_1.7_RU.md` and `BUILDING_RU.md`.

## Core policy
- Never silently downmix or reduce sample bit depth.
- When the current hook detects a format it cannot pass to OpenAL exactly, it logs `Exact OpenAL redirect unavailable` and keeps the original WASAPI path rather than intentionally converting the data.
- The GUI does not automatically open the detailed audio monitor after launch. Important format/status and warning messages are shown in the main status line; the detailed monitor opens only when requested.
- Native OpenAL spatial sources and completed multichannel PCM are different paths. Native OpenAL games should use Creative OpenAL directly where possible; PCM games need a compatible multichannel format path.

## Current limitations
- Experimental WASAPI→OpenAL PCM path passes selected PCM16/Float32 layouts (1/2/4/6/7/8 channels) without checking OpenAL extension strings first. OpenAL must expose an enum for the exact sample type and channel count; formats with no matching enum cannot be passed transparently without conversion.
- Up to 128 spatial sources/voices is not the same as 128 PCM channels; the native OpenAL call path must be forwarded transparently to preserve positions and parameters.
- WASAPI Exclusive is not implemented. ASIO support is experimental and rate/format restrictions remain.
- Log-based alerts only report incompatibilities that the hook actually detects. They cannot prove interception of a game that uses an unhooked API.
- No Windows compilation or physical-output bit-exact validation has been performed for this package. Driver/hardware DSP may alter playback after the application buffer is accepted.

## Build
See `BUILDING_RU.md`. Requires Windows, Visual Studio 2022 C++ workload, CMake, and .NET Framework 4.x compiler. ASIO SDK is not bundled.

## Changelog
See `CHANGELOG_2.0_RU.md`, `CHANGELOG_1.9_RU.md`, `CHANGELOG_1.8_RU.md` and previous changelogs.


## Version 2.1

See `CHANGELOG_2.1_RU.md` for exact format mapping, refusal behavior, and validation limits. This is source code, not a tested Windows binary.

## Version 2.2

See `CHANGELOG_2.2_RU.md`. The log is stored beside the Game Audio Bridge application. Windows build and hardware validation are still required.
