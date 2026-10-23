[CmdletBinding()]
param([string]$Destination = (Join-Path (Split-Path -Parent $PSScriptRoot) 'third_party\asio-sdk'))
$ErrorActionPreference = 'Stop'
$repo = 'https://github.com/audiosdk/asio.git'
if (Test-Path (Join-Path $Destination '.git')) {
  git -C $Destination fetch --depth 1 origin main
  if ($LASTEXITCODE -ne 0) { throw 'Git fetch failed.' }
  git -C $Destination checkout FETCH_HEAD
} elseif (Test-Path $Destination) {
  throw "Destination exists but is not a Git checkout: $Destination"
} else {
  git clone --depth 1 $repo $Destination
  if ($LASTEXITCODE -ne 0) { throw 'Git clone failed.' }
}
$license = Join-Path $Destination 'LICENSE.txt'
if (!(Test-Path $license)) { throw 'ASIO SDK LICENSE.txt was not found. Stop and review licensing.' }
Write-Host 'ASIO SDK source fetched. It is not wired into this application yet.'
Write-Host "Review $license and THIRD_PARTY_NOTICES.md before redistribution."
