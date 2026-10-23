#define MyAppName "Game Audio Bridge"
#define MyAppVersion "0.6 experimental"
#define MyAppPublisher "Game Audio Bridge contributors"
[Setup]
AppId={{4D8D6B67-1C79-48A8-8A4C-AB4A9C4D4D0F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\Game Audio Bridge
DefaultGroupName=Game Audio Bridge
OutputDir=..\Output
OutputBaseFilename=GameAudioBridge-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\README.txt
[Files]
Source: "..\dist\x64\*"; DestDir: "{app}\x64"; Flags: ignoreversion
Source: "..\dist\x86\*"; DestDir: "{app}\x86"; Flags: ignoreversion
Source: "..\dist\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\Launch-Bridge.ps1"; DestDir: "{app}"; Flags: ignoreversion
[Icons]
Name: "{group}\Game Audio Bridge instructions"; Filename: "{app}\README.txt"
Name: "{group}\Uninstall Game Audio Bridge"; Filename: "{uninstallexe}"
[Run]
Filename: "notepad.exe"; Parameters: """{app}\README.txt"""; Description: "Open usage instructions"; Flags: postinstall shellexec skipifsilent
