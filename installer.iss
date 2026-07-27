; Inno Setup script for MD Boss
; Build with:  iscc installer.iss   (produces installer\MDBoss-Setup.exe)
; Requires the app to be built first:  python -m PyInstaller MDBoss.spec
; Releases also ship installer\MDBoss-Portable-App.zip (the dist\MDBoss folder);
; release.ps1 does the whole cycle -- see the README's build section.

#define AppName "MD Boss"
#define AppVersion "0.1.11"
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

; One-dir build: the exe plus its _internal folder.
[Files]
Source: "dist\MDBoss\*"; DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; \
    Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; \
    GroupDescription: "Additional icons:"
Name: "associate"; Description: \
    "&Register MD Boss as a handler for Markdown files (.md)"; \
    GroupDescription: "File types:"

; The registry layout lives in app.py (registration_plan) and is applied by the
; exe itself, so the installer and the in-app "File types…" command cannot
; drift apart.  Per-user keys only -- no admin rights, and see app.py on why
; Windows still requires the user to pick the default themselves.
[Run]
Filename: "{app}\{#AppExe}"; Parameters: "--register-file-types"; \
    StatusMsg: "Registering Markdown file types..."; \
    Flags: runhidden waituntilterminated; Tasks: associate
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; \
    Flags: nowait postinstall skipifsilent

; Runs before the exe is deleted, and is harmless when nothing was registered.
[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--unregister-file-types"; \
    Flags: runhidden waituntilterminated; RunOnceId: "UnregisterFileTypes"
