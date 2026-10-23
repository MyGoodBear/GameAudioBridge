$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Write-Host 'Game Audio Bridge 0.4 (experimental)' -ForegroundColor Cyan
Write-Host 'Choose the architecture of the TARGET application, not Windows.'
Write-Host '1 = 64-bit application (x64)'
Write-Host '2 = 32-bit application (x86)'
$arch = Read-Host 'Architecture (1/2)'
if ($arch -eq '1') { $folder = 'x64' } elseif ($arch -eq '2') { $folder = 'x86' } else { throw 'Invalid architecture selection.' }
$rawPid = Read-Host 'Target process PID (find it in Task Manager > Details)'
$targetPid = 0
if (-not [int]::TryParse($rawPid, [ref]$targetPid) -or $targetPid -le 0) { throw 'Invalid PID.' }
Get-Process -Id $targetPid -ErrorAction Stop | Out-Null
$launcher = Join-Path $root "$folder\GameAudioBridgeLauncher.exe"
$dll = Join-Path $root "$folder\GameAudioBridgeHook.dll"
if (-not (Test-Path $launcher) -or -not (Test-Path $dll)) { throw 'Installed files are missing.' }
Write-Host 'Injection is experimental. Do not use with anti-cheat/protected processes.' -ForegroundColor Yellow
& $launcher $targetPid $dll
if ($LASTEXITCODE -ne 0) { Write-Warning "Launcher exited with code $LASTEXITCODE" }
Read-Host 'Press Enter to close'
