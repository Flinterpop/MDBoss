; Inno Setup script for MD Boss
; Build with:  iscc installer.iss   (produces installer\MDBoss-Setup.exe)
; Requires the app to be built first:  python -m PyInstaller MDBoss.spec
; Releases also ship installer\MDBoss-Portable.zip (just the exe, zipped);
; release.ps1 does the whole cycle -- see the README's build section.

#define AppName "MD Boss"
#define AppVersion "0.1.10"
#define AppExe "MDBoss.exe"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=RabidFox
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=installer
OutputBaseFilename=MDBoss-Setup
Compression=lzma2
SolidCompression=yes
; Per-user install so no admin rights are needed.
PrivilegesRequired=lowest
WizardStyle=modern

[Files]
Source: "dist\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; \
    Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; \
    GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; \
    Flags: nowait postinstall skipifsilent
