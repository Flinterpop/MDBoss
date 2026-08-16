; Inno Setup script for the MD Boss C++ port (MDBossCpp).
;
; Deliberately a SEPARATE script from installer.iss rather than an extension of
; it.  The Python build is the shipping product; this one is a parity pair
; still in development, and a bug here must not be able to break that release.
; It installs to its own folder with its own uninstall entry, so both can be
; present at once.
;
; Build with:  iscc installer-cpp.iss   (produces installer\MDBoss-Cpp-Setup.exe)
; Requires the app to be built first:
;   cmake --preset windows-static && cmake --build MDBossCpp\build --config Release
;
; ISCC lives at the non-default %LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe.

#define AppName "MD Boss (C++)"
#define AppVersion "1.4.3"
#define AppExe "MDBoss.exe"
#define BuildDir "MDBossCpp\build\app\Release"

[Setup]
; An explicit AppId keeps this uninstall entry distinct from the Python
; build's, which is derived from its own AppName.
AppId={{7C1B4E62-9E3A-4B21-8E37-2D5A9F0C41B8}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=RabidFox
DefaultDirName={autopf}\MD Boss Cpp
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=installer
OutputBaseFilename=MDBoss-Cpp-Setup
Compression=lzma2
SolidCompression=yes
; Ask per-user or per-machine at install time; per-machine (Program Files) is
; the recommended default, matching installer.iss.  A silent run takes the
; per-machine default, so the in-app updater passes /CURRENTUSER or /ALLUSERS
; to keep an update in the scope it is already installed in -- see
; install_scope_flag in MDBossCpp\app\Updater.cpp.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern

; A single static exe -- no _internal folder, because nothing is interpreted --
; plus the render assets it loads from beside itself.
[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "assets\*"; DestDir: "{app}\assets"; \
    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "HELP.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; \
    Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; \
    GroupDescription: "Additional icons:"
; UNCHECKED by default, unlike the Python installer.  Both builds claim the
; same ProgID (MDBoss.Markdown), so registering this one repoints every
; Markdown file at it -- which is not what someone trying out the port
; alongside their working install expects.
Name: "associate"; Description: "&Register this build as the Markdown (.md) handler - replaces the Python build's registration"; GroupDescription: "File types:"; Flags: unchecked

; The registry layout lives in MDBossCpp\app\FileAssoc.cpp and is applied by
; the exe itself, so the installer and the in-app "File types…" command cannot
; drift apart.  The keys are per-user (HKCU) whatever the install scope, so
; runasoriginaluser matters: an elevated per-machine install would otherwise
; write the elevating admin's hive, not the user's.
[Run]
Filename: "{app}\{#AppExe}"; Parameters: "--register-file-types"; \
    StatusMsg: "Registering Markdown file types..."; \
    Flags: runhidden waituntilterminated runasoriginaluser; Tasks: associate
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; \
    Flags: nowait postinstall skipifsilent

; Runs before the exe is deleted, and is harmless when nothing was registered.
; UninstallRun has no runasoriginaluser, so an elevated uninstall unregisters
; the elevating account's hive -- correct for the usual one-admin-user case.
[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--unregister-file-types"; \
    Flags: runhidden waituntilterminated; RunOnceId: "UnregisterFileTypes"
