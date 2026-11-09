; Optional native Windows installer; compile with Inno Setup 6 on Windows.
#ifndef AppVersion
  #define AppVersion "0.7.1"
#endif
#ifndef BundleDir
  #error "Set /DBundleDir to the native Windows PyInstaller onedir output"
#endif
#ifndef ArtifactDir
  #define ArtifactDir "..\..\dist\releases"
#endif

[Setup]
AppId={{B98EFC61-BCA0-4ACD-A492-97B3E6AE3181}
AppName=Cloud Stick Setup
AppVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\Cloud Stick
DefaultGroupName=Cloud Stick Setup
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
UninstallDisplayIcon={app}\cloud-stick.exe
OutputDir={#ArtifactDir}
OutputBaseFilename=cloud-stick-{#AppVersion}-windows-x86_64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes

[Files]
Source: "{#BundleDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\Cloud Stick Setup"; Filename: "{app}\cloud-stick.exe"
Name: "{userdesktop}\Cloud Stick Setup"; Filename: "{app}\cloud-stick.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Run]
Filename: "{app}\cloud-stick.exe"; Description: "Launch Cloud Stick Setup"; Flags: nowait postinstall skipifsilent


[UninstallRun]
Filename: "{app}\cloud-stick.exe"; Parameters: "--disable-startup"; Flags: runhidden; RunOnceId: "StopCloudStickWatcher"
