# Сборка Game Audio Bridge в Windows 10/11

## Требования
- Visual Studio 2022 (или Build Tools) с **Desktop development with C++**, MSVC x86/x64.
- CMake 3.21+.
- .NET Framework 4.x compiler (`csc.exe`).
- Для установщика — Inno Setup 6.
- Для загрузки ASIO SDK — Git и доступ к GitHub.

## Сборка
Откройте **Developer PowerShell for VS 2022** в корне проекта и выполните:

```powershell
.\build.ps1
```

Результаты будут в `dist\`, включая `dist\x86` и `dist\x64`. Для установщика:

```powershell
.\build.ps1 -BuildInstaller
```

Чтобы получить официальный исходный код Steinberg ASIO SDK под GPLv3:

```powershell
.\tools\Fetch-ASIO-SDK.ps1
```

Скрипт клонирует `https://github.com/audiosdk/asio` и сохраняет лицензию SDK. **SDK пока не интегрирован в аудиодвижок** этой версии; загрузка сама по себе не реализует ASIO.

## Статус функций
- OpenAL Creative: экспериментальный путь, требует проверки на целевой Windows-машине.
- WASAPI Exclusive: **не реализован**.
- ASIO Steinberg: **не реализован**; SDK только подготавливается скриптом.

Успешная компиляция не доказывает, что игровой звук реально перехватывается или выводится выбранным способом. Для этого нужны тесты на Windows 10, играх x86/x64 и реальном устройстве.
