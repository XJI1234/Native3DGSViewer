#ifndef StageDir
  #error StageDir is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif

[Setup]
AppId={{6F6D0E39-0AAE-45A3-960B-8A55A5A48ED2}
AppName=Native3DGS Cloud Viewer
AppVersion=0.2.2
AppPublisher=Native3DGSViewer
AppPublisherURL=https://github.com/XJI1234/Native3DGSViewer
DefaultDirName={localappdata}\Programs\Native3DGS Cloud Viewer
DefaultGroupName=Native3DGS Cloud Viewer
OutputDir={#OutputDir}
OutputBaseFilename=Native3DGSCloudViewer-0.2.2-Windows-x64-Setup
UninstallDisplayIcon={app}\Native3DGSCloud.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Native3DGS Cloud Viewer"; Filename: "{app}\Native3DGSCloud.exe"
Name: "{autodesktop}\Native3DGS Cloud Viewer"; Filename: "{app}\Native3DGSCloud.exe"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "创建云端查看器桌面快捷方式"; Flags: unchecked

[Run]
Filename: "{app}\Native3DGSCloud.exe"; Description: "启动 Native3DGS Cloud Viewer"; Flags: nowait postinstall skipifsilent
