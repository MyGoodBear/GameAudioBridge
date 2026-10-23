$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$configFile = Join-Path $root 'GameAudioBridge.ini'

# A game is represented by its full EXE path. Architecture is always detected from the PE header.
$script:enabled = New-Object 'System.Collections.Generic.List[object]'
$script:disabled = New-Object 'System.Collections.Generic.List[object]'
$script:displayMap = @{}

function Get-ExeArchitecture([string]$Path) {
    $stream = $null; $reader = $null
    try {
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $reader = New-Object System.IO.BinaryReader($stream)
        if ($stream.Length -lt 64) { return $null }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset -gt ($stream.Length - 6)) { return $null }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { return $null }
        $machine = $reader.ReadUInt16()
        switch ($machine) {
            0x014c { return 'x86' }
            0x8664 { return 'x64' }
            0xAA64 { return 'ARM64' }
            default { return $null }
        }
    } catch { return $null }
    finally { if ($reader) { $reader.Dispose() } elseif ($stream) { $stream.Dispose() } }
}

function New-GameEntry([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    if (!(Test-Path -LiteralPath $full -PathType Leaf)) { return $null }
    $arch = Get-ExeArchitecture $full
    if (!$arch) { return $null }
    return [pscustomobject]@{ Path = $full; Arch = $arch; Name = [System.IO.Path]::GetFileNameWithoutExtension($full) }
}

function Save-Config {
    $lines = New-Object 'System.Collections.Generic.List[string]'
    $lines.Add('; Game Audio Bridge game list. Managed by Launch-Bridge.ps1')
    $lines.Add('[Settings]')
    $lines.Add('Version=1')
    foreach ($section in @(@{ Name='Enabled'; Items=$script:enabled }, @{ Name='Disabled'; Items=$script:disabled })) {
        $lines.Add('')
        $lines.Add('[' + $section.Name + ']')
        $lines.Add('Count=' + $section.Items.Count)
        for ($i = 0; $i -lt $section.Items.Count; $i++) {
            $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes([string]$section.Items[$i].Path))
            $lines.Add("Game$i=$encoded")
        }
    }
    [System.IO.File]::WriteAllLines($configFile, $lines, (New-Object System.Text.UTF8Encoding($false)))
}

function Load-Config {
    if (!(Test-Path -LiteralPath $configFile)) { return }
    $section = ''
    foreach ($line in [System.IO.File]::ReadAllLines($configFile)) {
        $t = $line.Trim()
        if (!$t -or $t.StartsWith(';') -or $t.StartsWith('#')) { continue }
        if ($t -match '^\[(.+)\]$') { $section = $Matches[1]; continue }
        if ($section -notin @('Enabled','Disabled')) { continue }
        if ($t -match '^Game\d+=(.*)$') {
            try {
                $path = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Matches[1]))
                $entry = New-GameEntry $path
                if ($entry) {
                    $target = if ($section -eq 'Enabled') { $script:enabled } else { $script:disabled }
                    if (-not ((@($script:enabled.ToArray()) + @($script:disabled.ToArray())) | Where-Object { $_.Path -ieq $entry.Path })) { $target.Add($entry) }
                }
            } catch { }
        }
    }
}

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Game Audio Bridge 0.6 — управление играми'
$form.StartPosition = 'CenterScreen'
$form.Size = New-Object System.Drawing.Size(1040, 650)
$form.MinimumSize = New-Object System.Drawing.Size(900, 560)
$form.Font = New-Object System.Drawing.Font('Segoe UI', 9)

$top = New-Object System.Windows.Forms.Label
$top.Text = 'Добавляй EXE-файлы или сканируй папки. Разрядность определяется автоматически. Переноси игры между списками для включения/выключения перехвата.'
$top.SetBounds(14, 12, 990, 42)
$form.Controls.Add($top)

$disabledLabel = New-Object System.Windows.Forms.Label
$disabledLabel.Text = 'Перехват выключен'
$disabledLabel.SetBounds(14, 62, 430, 22)
$form.Controls.Add($disabledLabel)
$enabledLabel = New-Object System.Windows.Forms.Label
$enabledLabel.Text = 'Перехват включён'
$enabledLabel.SetBounds(575, 62, 430, 22)
$form.Controls.Add($enabledLabel)

$leftList = New-Object System.Windows.Forms.ListBox
$leftList.SetBounds(14, 88, 430, 390)
$leftList.Anchor = 'Top,Bottom,Left'
$leftList.HorizontalScrollbar = $true
$leftList.SelectionMode = 'MultiExtended'
$form.Controls.Add($leftList)
$rightList = New-Object System.Windows.Forms.ListBox
$rightList.SetBounds(575, 88, 430, 390)
$rightList.Anchor = 'Top,Bottom,Right'
$rightList.HorizontalScrollbar = $true
$rightList.SelectionMode = 'MultiExtended'
$form.Controls.Add($rightList)

$addExe = New-Object System.Windows.Forms.Button
$addExe.Text = 'Добавить EXE…'; $addExe.SetBounds(14, 493, 135, 32); $addExe.Anchor = 'Bottom,Left'; $form.Controls.Add($addExe)
$addFolder = New-Object System.Windows.Forms.Button
$addFolder.Text = 'Добавить папку…'; $addFolder.SetBounds(157, 493, 145, 32); $addFolder.Anchor = 'Bottom,Left'; $form.Controls.Add($addFolder)
$enable = New-Object System.Windows.Forms.Button
$enable.Text = 'Включить  →'; $enable.SetBounds(450, 205, 118, 36); $enable.Anchor = 'Top'; $form.Controls.Add($enable)
$disable = New-Object System.Windows.Forms.Button
$disable.Text = '←  Выключить'; $disable.SetBounds(450, 251, 118, 36); $disable.Anchor = 'Top'; $form.Controls.Add($disable)
$launch = New-Object System.Windows.Forms.Button
$launch.Text = 'Запустить выбранную игру с перехватом'; $launch.SetBounds(575, 493, 430, 32); $launch.Anchor = 'Bottom,Right'; $form.Controls.Add($launch)
$remove = New-Object System.Windows.Forms.Button
$remove.Text = 'Удалить из списка'; $remove.SetBounds(14, 533, 145, 30); $remove.Anchor = 'Bottom,Left'; $form.Controls.Add($remove)
$status = New-Object System.Windows.Forms.Label
$status.Text = 'Список сохраняется рядом с программой в GameAudioBridge.ini'; $status.SetBounds(170, 538, 650, 24); $status.Anchor = 'Bottom,Left,Right'; $form.Controls.Add($status)

function Format-Entry($entry) { return ('[{0}]  {1}  —  {2}' -f $entry.Arch, $entry.Name, $entry.Path) }
function Refresh-Lists {
    $leftList.BeginUpdate(); $rightList.BeginUpdate()
    $leftList.Items.Clear(); $rightList.Items.Clear(); $script:displayMap = @{}
    foreach ($e in $script:disabled) { $s = Format-Entry $e; [void]$leftList.Items.Add($s); $script:displayMap[$s] = $e }
    foreach ($e in $script:enabled) { $s = Format-Entry $e; [void]$rightList.Items.Add($s); $script:displayMap[$s] = $e }
    $leftList.EndUpdate(); $rightList.EndUpdate()
    $disabledLabel.Text = "Перехват выключен ($($script:disabled.Count))"
    $enabledLabel.Text = "Перехват включён ($($script:enabled.Count))"
}
function Add-Paths([string[]]$paths) {
    $added = 0; $skipped = 0
    foreach ($path in $paths) {
        $entry = New-GameEntry $path
        if (!$entry) { $skipped++; continue }
        if ((@($script:enabled.ToArray()) + @($script:disabled.ToArray())) | Where-Object { $_.Path -ieq $entry.Path }) { continue }
        $script:disabled.Add($entry); $added++
    }
    Refresh-Lists; Save-Config
    $status.Text = "Добавлено: $added; пропущено неподдерживаемых/не-EXE: $skipped."
}

$addExe.Add_Click({
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = 'Выбери одну или несколько игр (EXE)'
    $dlg.Filter = 'Исполняемые файлы (*.exe)|*.exe|Все файлы (*.*)|*.*'
    $dlg.Multiselect = $true; $dlg.CheckFileExists = $true
    if ($dlg.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { Add-Paths $dlg.FileNames }
})
$addFolder.Add_Click({
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = 'Выбери папку с играми. Будут найдены EXE-файлы во всех вложенных папках.'
    $dlg.ShowNewFolderButton = $false
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return }
    $status.Text = 'Поиск EXE-файлов…'
    $form.Refresh()
    try {
        $paths = @(Get-ChildItem -LiteralPath $dlg.SelectedPath -Filter '*.exe' -File -Recurse -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
        Add-Paths $paths
        $status.Text = "Проверено EXE: $($paths.Count). Неподдерживаемые архитектуры пропускаются."
    } catch { [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Ошибка сканирования папки', 'OK', 'Error') | Out-Null }
})
$enable.Add_Click({
    foreach ($s in @($leftList.SelectedItems)) { $e = $script:displayMap[[string]$s]; if ($e) { [void]$script:disabled.Remove($e); $script:enabled.Add($e) } }
    Refresh-Lists; Save-Config
})
$disable.Add_Click({
    foreach ($s in @($rightList.SelectedItems)) { $e = $script:displayMap[[string]$s]; if ($e) { [void]$script:enabled.Remove($e); $script:disabled.Add($e) } }
    Refresh-Lists; Save-Config
})
$remove.Add_Click({
    foreach ($s in @($leftList.SelectedItems)) { $e = $script:displayMap[[string]$s]; if ($e) { [void]$script:disabled.Remove($e) } }
    foreach ($s in @($rightList.SelectedItems)) { $e = $script:displayMap[[string]$s]; if ($e) { [void]$script:enabled.Remove($e) } }
    Refresh-Lists; Save-Config
})
$launch.Add_Click({
    if ($rightList.SelectedItems.Count -ne 1) {
        [System.Windows.Forms.MessageBox]::Show('Выбери ровно одну игру в списке «Перехват включён».', 'Game Audio Bridge', 'OK', 'Information') | Out-Null
        return
    }
    $entry = $script:displayMap[[string]$rightList.SelectedItem]
    if (!$entry) { return }
    if ($entry.Arch -notin @('x86','x64')) {
        [System.Windows.Forms.MessageBox]::Show("Архитектура $($entry.Arch) пока не поддерживается. Нужны x86 или x64.", 'Game Audio Bridge', 'OK', 'Warning') | Out-Null
        return
    }
    $launcher = Join-Path $root "$($entry.Arch)\GameAudioBridgeLauncher.exe"
    $dll = Join-Path $root "$($entry.Arch)\GameAudioBridgeHook.dll"
    if (!(Test-Path -LiteralPath $launcher) -or !(Test-Path -LiteralPath $dll)) {
        [System.Windows.Forms.MessageBox]::Show("Не найдены файлы перехвата $($entry.Arch). Проверь установку в папке:`n$root\$($entry.Arch)", 'Game Audio Bridge', 'OK', 'Error') | Out-Null
        return
    }
    $answer = [System.Windows.Forms.MessageBox]::Show("Игра:`n$($entry.Path)`n`nОпределённая разрядность: $($entry.Arch)`n`nЗапустить с экспериментальным перехватом? Не используйте с античитом или защищёнными процессами.", 'Game Audio Bridge', 'OKCancel', 'Warning')
    if ($answer -ne [System.Windows.Forms.DialogResult]::OK) { return }
    try {
        $args = '--launch "' + $entry.Path + '" "' + $dll + '"'
        Start-Process -FilePath $launcher -ArgumentList $args
        $status.Text = "Запущено: $($entry.Name) ($($entry.Arch))."
    } catch { [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Не удалось запустить игру', 'OK', 'Error') | Out-Null }
})

Load-Config
Refresh-Lists
[void]$form.ShowDialog()
