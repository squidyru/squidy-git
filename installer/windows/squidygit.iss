; Copyright (c) 2026 Sergey Yakunin <sergey@squidy.ru>
;
; Compiled by installer/windows/build-installer.cmake, which passes AppVersion,
; FileVersion, SourceDir, IconFile, LicenseFile, OutputDir and OutputBaseFilename.

#define AppName "SquidyGit"
#define AppExeName "SquidyGit.exe"
#define AppPublisher "Sergey Yakunin"
#define AppUrl "https://github.com/squidyru/squidy-git"

[Setup]
; The AppId ties every release to the same installation, so a new setup
; upgrades the existing one instead of installing a second copy.
AppId={{733BE696-A946-4D7D-BCA8-28F975881805}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
VersionInfoVersion={#FileVersion}
VersionInfoProductVersion={#FileVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
UsePreviousAppDir=yes
LicenseFile={#LicenseFile}
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Let the Restart Manager close a running SquidyGit during an update. Starting it
; again is done by the [Run] section, which keeps the new process unelevated.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent
; A silent installation is an update started from SquidyGit itself, so the
; application is brought back without asking.
Filename: "{app}\{#AppExeName}"; Flags: nowait runasoriginaluser; Check: WizardSilent

[InstallDelete]
; Leftovers of the Qt Installer Framework packages used up to 0.0.1-beta.5.
Type: filesandordirs; Name: "{app}\installerResources"
Type: files; Name: "{app}\components.xml"
Type: files; Name: "{app}\installer.dat"
Type: files; Name: "{app}\network.xml"
Type: files; Name: "{app}\SquidyGit.ini"

[Code]
// Installations made by the Qt Installer Framework keep their own maintenance
// tool and Programs and Features entry. Remove them before the first Inno Setup
// based installation, otherwise the old entry stays behind forever.
procedure RemoveLegacyInstallation;
var
  MaintenanceTool: String;
  ResultCode: Integer;
begin
  MaintenanceTool := ExpandConstant('{app}\Uninstall SquidyGit.exe');
  if not FileExists(MaintenanceTool) then
    Exit;

  Exec(MaintenanceTool, 'purge --confirm-command', '', SW_HIDE,
       ewWaitUntilTerminated, ResultCode);
  DeleteFile(MaintenanceTool);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  RemoveLegacyInstallation;
  Result := '';
end;
