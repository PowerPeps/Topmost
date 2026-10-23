; Inno Setup script for Topmost
; Build with: ISCC.exe installer.iss   (Inno Setup 6 - https://jrsoftware.org/isinfo.php)

#if !FileExists("Topmost.exe")
#  error Topmost.exe introuvable : lancer build.bat, pas ISCC directement.
#endif
#define AppVer GetVersionNumbersString("Topmost.exe")
#define HasArm64 FileExists("Topmost.arm64.exe") && FileExists("TopmostHookArm64.dll")
#define RunKey "Software\Microsoft\Windows\CurrentVersion\Run"

[Setup]
AppId={{7461285E-F947-4A30-8E4A-9F73A2BB9EF3}
AppName=Topmost
AppVersion={#AppVer}
VersionInfoVersion={#AppVer}
VersionInfoProductVersion={#AppVer}
VersionInfoCompany=Topmost
VersionInfoDescription=Topmost setup
AppPublisher=Topmost
DefaultDirName={autopf}\Topmost
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
#if HasArm64
ArchitecturesAllowed=x64compatible or arm64
ArchitecturesInstallIn64BitMode=x64compatible or arm64
#else
ArchitecturesAllowed=x64compatible and not arm64
ArchitecturesInstallIn64BitMode=x64compatible and not arm64
#endif
CloseApplications=no
OutputDir=.
OutputBaseFilename=Setup
Compression=lzma2
SolidCompression=yes
ShowLanguageDialog=no
WizardStyle=modern
SetupIconFile=Topmost.ico
UninstallDisplayName=Topmost
UninstallDisplayIcon={app}\Topmost.exe

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[CustomMessages]
en.AutoStart=Start Topmost with Windows
fr.AutoStart=Demarrer Topmost avec Windows
en.Launch=Launch Topmost
fr.Launch=Lancer Topmost

[Tasks]
Name: "autostart"; Description: "{cm:AutoStart}"

[Files]
#if HasArm64
Source: "Topmost.arm64.exe"; DestDir: "{app}"; DestName: "Topmost.exe"; Check: IsArm64; Flags: ignoreversion
Source: "TopmostHookArm64.dll"; DestDir: "{app}"; Check: IsArm64; Flags: ignoreversion
; x64 processes on ARM64 need their own hook host.
Source: "Topmost.exe"; DestDir: "{app}"; DestName: "Topmost64.exe"; Check: IsArm64; Flags: ignoreversion
Source: "Topmost.exe"; DestDir: "{app}"; Check: not IsArm64; Flags: ignoreversion
#else
Source: "Topmost.exe"; DestDir: "{app}"; Flags: ignoreversion
#endif
Source: "TopmostHook64.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Topmost32.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "TopmostHook32.dll"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Topmost"; Filename: "{app}\Topmost.exe"

[Registry]
Root: HKCU; Subkey: "{#RunKey}"; ValueType: string; ValueName: "Topmost"; ValueData: """{app}\Topmost.exe"""; Tasks: autostart
Root: HKCU; Subkey: "{#RunKey}"; ValueType: none; ValueName: "Topmost"; Flags: deletevalue; Tasks: not autostart

[Run]
Filename: "{app}\Topmost.exe"; Description: "{cm:Launch}"; Flags: nowait postinstall skipifsilent

[Code]
function GetTickCount: Cardinal;
  external 'GetTickCount@kernel32.dll stdcall';

// Lets the running instance remove its menu items and release its hooks.
procedure QuitRunning;
var
  rc: Integer;
begin
  if FileExists(ExpandConstant('{app}\Topmost.exe')) then
    Exec(ExpandConstant('{app}\Topmost.exe'), '/quit', '', SW_HIDE, ewWaitUntilTerminated, rc);
end;

// Once unhooked, the DLLs stay mapped in every hooked process until that process wakes up:
// they cannot be deleted yet, but a mapped file can still be renamed. Topmost.exe deletes
// the leftovers (*.old) at its next start.
procedure ParkFiles(const Pattern, DestDir: String);
var
  fr: TFindRec;
  src: String;
begin
  if not FindFirst(ExpandConstant('{app}\') + Pattern, fr) then Exit;
  try
    repeat
      src := ExpandConstant('{app}\') + fr.Name;
      if not DeleteFile(src) then
        RenameFile(src, AddBackslash(DestDir) + fr.Name + '.' + IntToStr(GetTickCount) + '.old');
    until not FindNext(fr);
  finally
    FindClose(fr);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  QuitRunning;
  ParkFiles('TopmostHook*.dll', ExpandConstant('{app}'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    QuitRunning;
    ParkFiles('TopmostHook*.dll', GetTempDir);
    ParkFiles('*.old', GetTempDir);
  end;
  if CurUninstallStep = usPostUninstall then
    RegDeleteValue(HKEY_CURRENT_USER, '{#RunKey}', 'Topmost');
end;
