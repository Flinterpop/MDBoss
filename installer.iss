; Inno Setup script for MD Boss
; Build with:  iscc installer.iss   (produces installer\MDBoss-Setup.exe)
; Requires the app to be built first:  python -m PyInstaller MDBoss.spec
; Releases also ship installer\MDBoss-Portable-App.zip (the dist\MDBoss folder);
; release.ps1 does the whole cycle -- see the README's build section.

#define AppName "MD Boss"
#define AppVersion "1.2.0"
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
; Ask per-user or per-machine at install time; per-machine (Program Files) is
; the recommended default.  {autopf} follows the answer: Program Files when
; elevated, %LOCALAPPDATA%\Programs when not.  A silent run takes the
; per-machine default, so the in-app updater passes /CURRENTUSER or /ALLUSERS
; to keep an update in the scope it is already installed in -- see
; _install_scope_flag in app.py.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
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
; drift apart.  The keys are per-user (HKCU) whatever the install scope, so
; runasoriginaluser matters: an elevated per-machine install would otherwise
; run this as the elevating admin and write that account's hive, not the
; user's.  See app.py on why Windows still requires the user to pick the
; default themselves.
[Run]
Filename: "{app}\{#AppExe}"; Parameters: "--register-file-types"; \
    StatusMsg: "Registering Markdown file types..."; \
    Flags: runhidden waituntilterminated runasoriginaluser; Tasks: associate
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; \
    Flags: nowait postinstall skipifsilent

; Runs before the exe is deleted, and is harmless when nothing was registered.
; UninstallRun has no runasoriginaluser, so an elevated uninstall unregisters
; the elevating account's hive -- correct for the usual one-admin-user case,
; and at worst it leaves another user's HKCU entries pointing at a gone exe.
[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--unregister-file-types"; \
    Flags: runhidden waituntilterminated; RunOnceId: "UnregisterFileTypes"
