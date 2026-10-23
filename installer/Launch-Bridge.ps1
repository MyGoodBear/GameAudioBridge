$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Game Audio Bridge 0.5'
$form.Width = 560; $form.Height = 190
$form.StartPosition = 'CenterScreen'
$label = New-Object System.Windows.Forms.Label
$label.Left = 18; $label.Top = 18; $label.Width = 510; $label.Height = 42
$label.Text = 'Выбери EXE-файл игры или приложения. Перехват будет загружен до запуска процесса; PID искать не нужно.'
$form.Controls.Add($label)
$button = New-Object System.Windows.Forms.Button
$button.Left = 18; $button.Top = 78; $button.Width = 510; $button.Height = 38
$button.Text = 'Выбрать приложение и запустить с перехватом'
$form.Controls.Add($button)
$button.Add_Click({
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Выбери EXE игры или приложения'
    $dialog.Filter = 'Программы Windows (*.exe)|*.exe'
    $dialog.CheckFileExists = $true
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return }
    $exe = $dialog.FileName
    # Read PE machine field so the matching injector/DLL architecture is used.
    $stream = [System.IO.File]::OpenRead($exe)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset -gt ($stream.Length - 6)) { throw 'Файл не является корректным PE EXE.' }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw 'Не найден заголовок PE.' }
        $machine = $reader.ReadUInt16()
    } finally { $stream.Dispose() }
    switch ($machine) {
        0x8664 { $arch = 'x64' }
        0x014c { $arch = 'x86' }
        default { [System.Windows.Forms.MessageBox]::Show(("Архитектура EXE не поддерживается (PE machine: 0x{0:X4})." -f $machine), 'Game Audio Bridge') | Out-Null; return }
    }
    $launcher = Join-Path $root "$arch\GameAudioBridgeLauncher.exe"
    $dll = Join-Path $root "$arch\GameAudioBridgeHook.dll"
    if (!(Test-Path $launcher) -or !(Test-Path $dll)) { [System.Windows.Forms.MessageBox]::Show("Не найдены файлы для архитектуры $arch.", 'Game Audio Bridge') | Out-Null; return }
    $confirm = [System.Windows.Forms.MessageBox]::Show("Приложение:`n$exe`n`nАрхитектура: $arch`n`nЗапустить с экспериментальным аудиоперехватом? Не используй с античитом или защищёнными процессами.", 'Game Audio Bridge', 'OKCancel', 'Warning')
    if ($confirm -ne [System.Windows.Forms.DialogResult]::OK) { return }
    $form.Close()
    Start-Process -FilePath $launcher -ArgumentList @('--launch', ('"' + $exe + '"'), ('"' + $dll + '"')) -Wait
})
[void]$form.ShowDialog()
