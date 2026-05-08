#define MyAppName "O-Walkie Desktop"
#define MyAppExeName "owalkie-desktop.exe"
#define _EnvVersion GetEnv("OWALKIE_VERSION_STRING")
#if _EnvVersion == ""
  #define MyAppVersion "0.0.0-dev"
#else
  #define MyAppVersion _EnvVersion
#endif

[Setup]
AppId={{6F876D74-2BB6-4E8E-A838-4F3D4EF6E514}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Outsidepro Arts
AppPublisherURL=https://github.com/outsidepro-arts/O-Walkie
AppSupportURL=https://github.com/outsidepro-arts/O-Walkie
AppUpdatesURL=https://github.com/outsidepro-arts/O-Walkie/releases
DefaultDirName={autopf}\O-Walkie Desktop
DefaultGroupName=O-Walkie Desktop
AllowNoIcons=yes
LicenseFile=..\..\LICENSE
OutputDir=..\build\installer
OutputBaseFilename=owalkie-desktop-setup-{#MyAppVersion}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\O-Walkie Desktop"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall O-Walkie Desktop"; Filename: "{uninstallexe}"
Name: "{autodesktop}\O-Walkie Desktop"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCR; Subkey: "owalkie"; ValueType: string; ValueName: ""; ValueData: "URL:O-Walkie Protocol"; Flags: uninsdeletekey
Root: HKCR; Subkey: "owalkie"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCR; Subkey: "owalkie\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKCR; Subkey: "owalkie\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
