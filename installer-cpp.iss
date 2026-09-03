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
#define AppVersion "1.15.0"
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
; The app is x64.  Without these two, Inno runs in 32-bit mode and {autopf}
; resolves to Program Files (x86) -- which is where every build up to and
; including v1.8.0 installed an x64 exe.  x64compatible rather than x64 so an
; Arm64 machine running x64 under emulation is not locked out.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

; A single static exe -- no _internal folder, because nothing is interpreted --
; plus the render assets it loads from beside itself.
[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
; template.html and pygments-github.css belong to the DEPRECATED Python app and
; are not shipped: template.html's scroll bridge loads Qt's
; qrc:///qtwebchannel/qwebchannel.js, which nothing in this build has, and the
; C++ page never references the Pygments sheet -- test_mdrender.cpp asserts
; exactly that.  They stay in the tree so mdrender.py still runs for the local
; archaeology CLAUDE.md keeps it for; they simply stop riding along in every
; install.  release.ps1 drops the same two from the portable zip.
Source: "assets\*"; DestDir: "{app}\assets"; \
    Excludes: "template.html,pygments-github.css"; \
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

; Every build up to v1.8.0 installed in 32-bit mode, so its uninstall entry is
; in the 32-bit registry view and this one -- now 64-bit -- cannot see it.
; Without this, upgrading would leave the old copy in Program Files (x86)
; complete with its own uninstaller and Start Menu shortcuts, while the new one
; landed in Program Files: two installs, two update paths, one of them stale.
[Code]
const
  LegacyKey =
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\' +
    '{7C1B4E62-9E3A-4B21-8E37-2D5A9F0C41B8}_is1';

function LegacyUninstaller(var Uninstaller: String): Boolean;
begin
  // Per-machine first, then per-user: both were written to the 32-bit view.
  Result := RegQueryStringValue(HKLM32, LegacyKey, 'UninstallString',
                                Uninstaller);
  if not Result then
    Result := RegQueryStringValue(HKCU32, LegacyKey, 'UninstallString',
                                  Uninstaller);
  if Result then
    Uninstaller := RemoveQuotes(Uninstaller);
  Result := Result and (Uninstaller <> '') and FileExists(Uninstaller);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Uninstaller: String;
  ResultCode: Integer;
  Waited: Integer;
begin
  // Best effort, never fatal: returning a message here ABORTS the install, and
  // failing to tidy up an old copy is not a reason to leave the user with no
  // new one.
  Result := '';
  if not LegacyUninstaller(Uninstaller) then
    Exit;

  if not Exec(Uninstaller, '/VERYSILENT /NORESTART /SUPPRESSMSGBOXES', '',
              SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Exit;

  // Inno's uninstaller re-launches itself from a temp copy, so the call above
  // returns before the work is done.  Wait for the entry to actually go, or
  // the old uninstaller deletes the Start Menu group we are about to fill --
  // both builds use the same group name.  Bounded: 60 x 500 ms, then give up
  // and install anyway.
  Waited := 0;
  while (Waited < 60) and LegacyUninstaller(Uninstaller) do
  begin
    Sleep(500);
    Waited := Waited + 1;
  end;
end;
