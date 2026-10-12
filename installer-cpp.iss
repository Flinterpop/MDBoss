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
; ISCC lives at C:\bin\InnoSetup6\ISCC.exe (not on PATH); release.ps1 finds it.

#define AppName "MD Boss (C++)"
#define AppVersion "1.17.0"
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
; Machine-level only (house rule, 11 Oct 2026): Program Files, admin, and no
; per-user choice -- so no PrivilegesRequiredOverridesAllowed directive at
; all.  Setup starts as the invoker and raises its own UAC prompt, which is
; what lets the in-app updater run it from an unelevated batch; that batch
; always passes /ALLUSERS (kInstallScopeFlag in MDBossCpp\app\Updater.h).
; A copy installed per-user by an older build is removed by [Code] below.
PrivilegesRequired=admin
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
; drift apart.  The keys are per-user (HKCU) although the install is
; per-machine, so runasoriginaluser matters: the elevated install would
; otherwise write the elevating admin's hive, not the user's.
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

; Two kinds of old copy this install replaces rather than sits beside, each
; found by its uninstall entry under this same AppId:
;
;  * Per-machine, 32-bit view (HKLM32): every build up to v1.8.0 installed in
;    32-bit mode, into Program Files (x86), where this 64-bit install cannot
;    see it.
;  * Per-user (HKCU): builds before the machine-only rule (11 Oct 2026) could
;    install into the user's own profile.  HKCU\Software is shared by both
;    registry views, so one lookup covers 32- and 64-bit builds alike.
;
; Left alone, either would stay complete with its own uninstaller, Start Menu
; shortcuts and update path while the new copy landed in Program Files: two
; installs, one of them stale.  Each is uninstalled silently first.
;
; HKCU here is the hive of the account Setup runs as.  When the user is an
; administrator who accepts the UAC prompt, that is their own hive and the
; per-user copy is found.  When a standard user elevates with ANOTHER
; account's credentials, it is that account's hive, nothing is found, and the
; per-user copy is left in place -- still working, still in that user's Apps
; and Features, and removable from there.  Inno cannot read the original
; user's hive from an elevated Setup, so that case is reported, not fixed.
;
; Uninstalling an old copy runs its --unregister-file-types, so a copy that
; had registered as the Markdown handler would silently stop being one.  Its
; recorded task list says whether it had; if so, the new exe registers again
; after the install, as the original user (whose hive the keys belong in).
[Code]
const
  UninstallKey =
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\' +
    '{7C1B4E62-9E3A-4B21-8E37-2D5A9F0C41B8}_is1';

var
  ReRegisterFileTypes: Boolean;

function OldUninstaller(const RootKey: Integer;
                        var Uninstaller: String): Boolean;
begin
  Result := RegQueryStringValue(RootKey, UninstallKey, 'UninstallString',
                                Uninstaller);
  if Result then
    Uninstaller := RemoveQuotes(Uninstaller);
  Result := Result and (Uninstaller <> '') and FileExists(Uninstaller);
end;

function OldCopyAssociated(const RootKey: Integer): Boolean;
var
  Tasks: String;
begin
  Result := RegQueryStringValue(RootKey, UninstallKey,
                                'Inno Setup: Selected Tasks', Tasks) and
            (Pos('associate', Lowercase(Tasks)) > 0);
end;

procedure RemoveOldCopy(const RootKey: Integer);
var
  Uninstaller: String;
  ResultCode: Integer;
  Waited: Integer;
begin
  // Best effort, never fatal: failing to tidy up an old copy is not a reason
  // to leave the user with no new one.
  if not OldUninstaller(RootKey, Uninstaller) then
    Exit;
  if OldCopyAssociated(RootKey) then
    ReRegisterFileTypes := True;

  if not Exec(Uninstaller, '/VERYSILENT /NORESTART /SUPPRESSMSGBOXES', '',
              SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Exit;

  // Inno's uninstaller re-launches itself from a temp copy, so the call above
  // returns before the work is done.  Wait for the entry to actually go, or
  // the old uninstaller can delete the Start Menu group we are about to fill
  // -- the builds share the group name.  Bounded: 60 x 500 ms, then give up
  // and install anyway.
  Waited := 0;
  while (Waited < 60) and OldUninstaller(RootKey, Uninstaller) do
  begin
    Sleep(500);
    Waited := Waited + 1;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  // Returning a message here ABORTS the install, so this always returns ''.
  Result := '';
  ReRegisterFileTypes := False;
  RemoveOldCopy(HKLM32);
  RemoveOldCopy(HKCU);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  // The [Run] entry already registers when the task is ticked; this covers
  // the silent update, where it is not ticked by default.  As the original
  // user, for the same reason the [Run] entry is runasoriginaluser.
  if (CurStep = ssPostInstall) and ReRegisterFileTypes and
     not WizardIsTaskSelected('associate') then
  begin
    if not ExecAsOriginalUser(ExpandConstant('{app}\{#AppExe}'),
                              '--register-file-types', '', SW_HIDE,
                              ewWaitUntilTerminated, ResultCode) then
      Log('Could not re-register the Markdown file types.');
  end;
end;
