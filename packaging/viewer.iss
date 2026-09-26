#ifndef StageDir
  #error StageDir is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif

[Setup]
AppId={{D7C155A0-3A8A-4D9A-98C2-5869E3CBF043}
AppName=3DGS Viewer
AppVersion=0.1.0
AppPublisher=Native3DGSViewer
DefaultDirName={localappdata}\Programs\3DGS Viewer
DefaultGroupName=3DGS Viewer
OutputDir={#OutputDir}
OutputBaseFilename=Native3DGSViewer-Setup-x64
UninstallDisplayIcon={app}\Native3DGSViewer.GUI.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\3DGS Viewer"; Filename: "{app}\Native3DGSViewer.GUI.exe"
Name: "{autodesktop}\3DGS Viewer"; Filename: "{app}\Native3DGSViewer.GUI.exe"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "创建桌面快捷方式"; GroupDescription: "快捷方式："; Flags: unchecked

[Run]
Filename: "{app}\Native3DGSViewer.GUI.exe"; Description: "启动 3DGS Viewer"; Flags: nowait postinstall skipifsilent
