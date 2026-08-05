; ---------------------------------------------------------------------------
;  DF-UpscalerVideo -- Inno Setup 6 script
;
;  Packages the CMake install tree (dist\), which build.bat produces via
;  `cmake --install`. Run through build.bat rather than directly, so that the
;  staged layout is guaranteed to match the binaries.
;
;      build.bat release --installer
; ---------------------------------------------------------------------------

#define AppName        "DF-UpscalerVideo"
#define AppVersion     "0.1.0"
#define AppPublisher   "DF-UpscalerVideo"
#define AppExeName     "DF-UpscalerVideo.exe"
#define AppCliExeName  "DF-UpscalerVideo-cli.exe"
#define DistDir        "..\dist"
#define IconFile       "..\resources\icons\app.ico"

#if !DirExists(AddBackslash(SourcePath) + DistDir)
  #error dist\ was not found. Run build.bat first so the install tree exists.
#endif

[Setup]
; Never change AppId: it is what lets an upgrade replace an existing install
; instead of piling up side by side.
AppId={{3516A36C-F4D9-4C13-85A5-A3575A39B5A9}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#AppVersion}

DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
AllowNoIcons=yes

; Per-user installs stay possible for users without admin rights.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

OutputDir=..\dist-installer
OutputBaseFilename={#AppName}-{#AppVersion}-setup
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName} {#AppVersion}

; LZMA2/max with solid compression: the Qt DLLs and the ncnn model weights
; compress well together, and installer size is a hard requirement.
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; Third-party licensing, chiefly the bundled FFmpeg build.
InfoBeforeFile=THIRD-PARTY.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The whole staged tree: both executables, the Qt runtime that windeployqt
; placed there, bin\ffmpeg and models\.
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "THIRD-PARTY.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Logs are application output, not user settings, so they go. The settings
; themselves live in HKCU\Software\DF-UpscalerVideo and are deliberately left
; in place so a reinstall keeps the user's configuration.
Type: filesandordirs; Name: "{userappdata}\{#AppName}\{#AppName}\logs"
Type: dirifempty;     Name: "{userappdata}\{#AppName}\{#AppName}"
Type: dirifempty;     Name: "{userappdata}\{#AppName}"

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if not FileExists(ExpandConstant('{app}\bin\ffmpeg.exe')) then
      MsgBox('This build was packaged without FFmpeg.' + #13#10#13#10 +
             'ffmpeg.exe and ffprobe.exe must be present in the bin folder ' +
             'before video processing will work.',
             mbInformation, MB_OK);
  end;
end;
