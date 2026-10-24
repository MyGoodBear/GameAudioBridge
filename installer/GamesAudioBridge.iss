#define MyAppName "GamesAudioBridge"
#define MyAppVersion "2.6"
#define MyAppPublisher "GamesAudioBridge contributors"
[Setup]
AppId={{4D8D6B67-1C79-48A8-8A4C-AB4A9C4D4D0F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\GamesAudioBridge
DefaultGroupName=GamesAudioBridge
OutputDir=..\Output
OutputBaseFilename=GamesAudioBridge-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\GamesAudioBridge.exe
[Files]
Source: "..\dist\GamesAudioBridge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\GamesAudioBridge.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist
Source: "..\dist\GamesAudioBridgeAsio.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\x64\*"; DestDir: "{app}\x64"; Flags: ignoreversion
Source: "..\dist\x86\*"; DestDir: "{app}\x86"; Flags: ignoreversion
Source: "..\dist\README.txt"; DestDir: "{app}"; Flags: ignoreversion
[Icons]
Name: "{group}\GamesAudioBridge"; Filename: "{app}\GamesAudioBridge.exe"
Name: "{group}\GamesAudioBridge instructions"; Filename: "{app}\README.txt"
Name: "{group}\Uninstall GamesAudioBridge"; Filename: "{uninstallexe}"
[Run]
Filename: "{app}\GamesAudioBridge.exe"; Description: "Launch GamesAudioBridge"; Flags: postinstall nowait skipifsilent
