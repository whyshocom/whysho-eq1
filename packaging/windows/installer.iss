; =============================================================================
;  whysho EQ1 — Windows installer (Inno Setup 6)
; =============================================================================
;
;  This script packages the VST3 plug-in and the Standalone application into a
;  single Windows installer (.exe).
;
;  It is driven by preprocessor defines that scripts/build-windows-installer.ps1
;  passes on the ISCC command line, e.g.:
;
;    ISCC.exe /DAppVersion=1.2.3 ^
;             /DBuildDir="C:\path\to\build" ^
;             /DRepoRoot="C:\path\to\repo" ^
;             /DOutputDir="C:\path\to\dist" ^
;             packaging\windows\installer.iss
;
;  Any define left unset falls back to a sensible default resolved relative to
;  this script's own location ({#SourcePath}), so the file can also be opened
;  and compiled directly from the Inno Setup IDE for local testing.
; =============================================================================

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

; Repo root (default: two levels up from packaging\windows\).
#ifndef RepoRoot
  #define RepoRoot AddBackslash(SourcePath) + "..\.."
#endif

; Build output tree (default: <RepoRoot>\build).
#ifndef BuildDir
  #define BuildDir AddBackslash(RepoRoot) + "build"
#endif

; Where the finished installer .exe is written (default: <RepoRoot>\dist).
#ifndef OutputDir
  #define OutputDir AddBackslash(RepoRoot) + "dist"
#endif

; EULA shown on the license page.
#ifndef LicenseFile
  #define LicenseFile AddBackslash(RepoRoot) + "packaging\EULA.txt"
#endif

#define AppName       "whysho EQ1"
#define AppPublisher  "whysho"
#define AppURL        "https://whysho.com"
#define AppExeName    "whysho EQ1.exe"
; JUCE artefact locations under the build tree.
#define ArtefactsDir  AddBackslash(BuildDir) + "WhyshoEQ1_artefacts\Release"
#define Vst3Bundle    AddBackslash(ArtefactsDir) + "VST3\whysho EQ1.vst3"
#define StandaloneExe AddBackslash(ArtefactsDir) + "Standalone\whysho EQ1.exe"

[Setup]
; A stable, unique AppId (GUID) so upgrades and uninstall work across versions.
AppId={{8F3C2A1E-7B64-4D9A-9C21-1E5A4F0B7D33}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoProductName={#AppName}

DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}
DisableProgramGroupPage=yes
LicenseFile={#LicenseFile}

; 64-bit only (Windows 10+ x64).
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

; Modern wizard UI and a clean uninstaller entry.
WizardStyle=modern
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#AppExeName}
Compression=lzma2/max
SolidCompression=yes
PrivilegesRequired=admin

OutputDir={#OutputDir}
OutputBaseFilename=WhyshoEQ1-{#AppVersion}-Windows

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plug-in";         Types: full custom; Flags: checkablealone
Name: "standalone"; Description: "Standalone application"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: standalone; Flags: unchecked

[Files]
; --- VST3 plug-in bundle -> C:\Program Files\Common Files\VST3 -----------------
; The .vst3 is a bundle (a directory tree); install it recursively.
Source: "{#Vst3Bundle}\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

; --- Standalone application -> Program Files -----------------------------------
Source: "{#StandaloneExe}"; DestDir: "{app}"; DestName: "{#AppExeName}"; \
    Components: standalone; Flags: ignoreversion

[Icons]
; Start Menu shortcut for the Standalone app.
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Components: standalone
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
; Optional desktop shortcut (controlled by the [Tasks] entry above).
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Components: standalone; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; \
    Flags: nowait postinstall skipifsilent; Components: standalone

[UninstallDelete]
; Remove the VST3 bundle directory on uninstall (recursesubdirs leaves the folder).
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"
