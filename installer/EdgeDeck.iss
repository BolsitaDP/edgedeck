; EdgeDeck's setup program: one EdgeDeck-<version>-setup-<arch>.exe for people who expect a
; double-click and a Next button. Built by scripts\installer.ps1 (and the release workflow) with
; Inno Setup 6; the zip with Install.cmd stays for those who prefer it, and winget uses the zip.
;
; It installs exactly what the zip's installer does, where the app expects it: EdgeDeck.exe in
; %LOCALAPPDATA%\EdgeDeck\bin, for the current user only and without administrator rights. The
; "Start with Windows" entry is the very one EdgeDeck's own menu toggles (HKCU Run, value
; "EdgeDeck", a quoted path), so the app and the installer always agree on it. Settings in
; %LOCALAPPDATA%\EdgeDeck are kept by an uninstall, as they are by uninstall.ps1.
;
; Defines (ISCC /D...): AppVersion (major.minor.patch), Arch (x64 or arm64), SourceExe (the built
; EdgeDeck.exe), OutputDir, Homepage.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef SourceExe
  #error SourceExe must be defined: the EdgeDeck.exe to install
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
#ifndef Homepage
  #define Homepage "https://github.com/BolsitaDP/edgedeck"
#endif

[Setup]
; Never change: it is how Windows and a later setup recognise an installed EdgeDeck.
AppId={{F1EA497D-8306-49A6-A42A-9E36995720E8}
AppName=EdgeDeck
AppVersion={#AppVersion}
AppVerName=EdgeDeck {#AppVersion}
AppPublisher=EdgeDeck
AppPublisherURL={#Homepage}
AppSupportURL={#Homepage}/issues
AppUpdatesURL={#Homepage}/releases/latest
VersionInfoVersion={#AppVersion}
; Where the app looks for its installed copy (Autostart::InstalledPath); not a choice to offer.
DefaultDirName={localappdata}\EdgeDeck\bin
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=lowest
#if Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=EdgeDeck-{#AppVersion}-setup-{#Arch}
SetupIconFile=..\res\EdgeDeck.ico
UninstallDisplayIcon={app}\EdgeDeck.exe
UninstallDisplayName=EdgeDeck
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; A running EdgeDeck holds EdgeDeck.exe open. [Code] asks it to exit first, as its menu's Exit
; would; the Restart Manager is the fallback (EdgeDeck exits on WM_ENDSESSION with
; ENDSESSION_CLOSEAPP). [Run] starts the new one.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[CustomMessages]
en.AutostartTask=Start EdgeDeck with Windows
es.AutostartTask=Iniciar EdgeDeck con Windows

[Tasks]
Name: "autostart"; Description: "{cm:AutostartTask}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; DestName: "EdgeDeck.exe"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\EdgeDeck"; Filename: "{app}\EdgeDeck.exe"
Name: "{userdesktop}\EdgeDeck"; Filename: "{app}\EdgeDeck.exe"; Tasks: desktopicon

[Registry]
; The zip's installer lists EdgeDeck in installed apps under a key of its own; this setup's entry
; replaces it, so the list does not show EdgeDeck twice.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Uninstall\EdgeDeck"; ValueType: none; Flags: deletekey
; Start with Windows, when asked for. Quoted, as the app writes it.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "EdgeDeck"; ValueData: """{app}\EdgeDeck.exe"""; Tasks: autostart
; Gone on uninstall whoever made it - this setup or EdgeDeck's menu - with Task Manager's on/off
; record of it.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "EdgeDeck"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run"; ValueType: none; ValueName: "EdgeDeck"; Flags: uninsdeletevalue

[Run]
; Not skipped when silent: a silent update closed EdgeDeck, and should leave it running again.
Filename: "{app}\EdgeDeck.exe"; Description: "{cm:LaunchProgram,EdgeDeck}"; Flags: nowait postinstall

[UninstallDelete]
; What the zip's installer left beside the program, if this replaced one - its uninstaller and the
; symbols of a build installed with install.ps1 -Build - and then the folder, if nothing else is in it.
Type: files; Name: "{app}\uninstall.ps1"
Type: files; Name: "{app}\EdgeDeck.pdb"
Type: dirifempty; Name: "{app}"

[Code]
const
  WM_CLOSE = $0010;

// EdgeDeck's own hidden window: closing it is a clean exit - the notification-area icon removed,
// the shortcuts released - where being terminated would leave a dead icon behind.
procedure CloseRunningEdgeDeck();
var
  Tries: Integer;
  Window: HWND;
begin
  Window := FindWindowByClassName('EdgeDeckUtilityWindow');
  if Window = 0 then
    Exit;
  PostMessage(Window, WM_CLOSE, 0, 0);
  Tries := 0;
  while (FindWindowByClassName('EdgeDeckUtilityWindow') <> 0) and (Tries < 50) do
  begin
    Sleep(100);
    Tries := Tries + 1;
  end;
  // A moment more for the process to let go of EdgeDeck.exe after its window is gone.
  Sleep(300);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  CloseRunningEdgeDeck();
  Result := '';
end;

function InitializeUninstall(): Boolean;
begin
  CloseRunningEdgeDeck();
  Result := True;
end;