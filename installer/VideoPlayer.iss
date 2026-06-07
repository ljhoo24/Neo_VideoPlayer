; ============================================================
; VideoPlayer — Inno Setup installer script
;
; Builds a single setup.exe that installs the app + all bundled
; Qt / libmpv runtime DLLs to Program Files, creates Start-menu /
; (optional) desktop shortcuts, registers VideoPlayer as a Windows
; default-app candidate (machine-wide HKLM), installs the VC++
; runtime, and ships an uninstaller.
;
; Compile:
;   ISCC.exe /DSrcDir="<path to build\release>" installer\VideoPlayer.iss
;
; SrcDir must point at a windeployqt-deployed Release build directory
; (the one containing VideoPlayer.exe + Qt6*.dll + mpv-2.dll).
; ============================================================

#define MyAppName        "VideoPlayer"
#define MyAppVersion     "1.0.5"
#define MyAppPublisher   "CustomMedia"
#define MyAppExeName     "VideoPlayer.exe"
#define MyProgId         "CustomMedia.VideoPlayer"

; Source build directory — override with /DSrcDir=...
#ifndef SrcDir
  #define SrcDir "..\build\release"
#endif

; Repo root (this script lives in installer\)
#define RepoRoot ".."

[Setup]
; A stable AppId keeps upgrades/uninstall consistent across versions.
AppId={{8F3D2A1C-5B6E-4C7D-9E0A-1F2B3C4D5E6F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir={#RepoRoot}\installer\Output
OutputBaseFilename=VideoPlayer-{#MyAppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Default-app registration writes to HKLM -> needs elevation
PrivilegesRequired=admin
SetupIconFile={#RepoRoot}\icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "korean";  MessagesFile: "compiler:Languages\Korean.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Whole deployed build tree, minus CMake/build leftovers.
Source: "{#SrcDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion; \
  Excludes: "CMakeFiles\*,*.lib,*.exp,*.pdb,*.ilk,*.manifest,CMakeCache.txt,cmake_install.cmake,build.ninja,.ninja_*,*.cmake"

[Icons]
Name: "{group}\{#MyAppName}";          Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}";    Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; Install the VC++ runtime that windeployqt bundled (no-op if newer present).
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
  StatusMsg: "Installing Visual C++ runtime..."; Flags: waituntilterminated skipifdoesntexist
; Offer to launch after install.
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; \
  Flags: nowait postinstall skipifsilent

[Registry]
; ---- ProgID: how Windows launches an associated file ----
Root: HKLM; Subkey: "Software\Classes\{#MyProgId}"; ValueType: string; ValueName: ""; ValueData: "VideoPlayer Media File"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\{#MyProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKLM; Subkey: "Software\Classes\{#MyProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

; ---- App Capabilities (feeds Settings > Default apps) ----
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#MyAppName}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Qt6 + libmpv media player"

; ---- RegisteredApplications ----
Root: HKLM; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: "Software\{#MyAppName}\Capabilities"; Flags: uninsdeletevalue

; ---- File associations (one pair per supported extension) ----
; Capabilities\FileAssociations entry + OpenWithProgids back-reference.
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mp4";  ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mkv";  ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".avi";  ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mov";  ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".wmv";  ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".flv";  ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".webm"; ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m4v";  ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ts";   ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".m2ts"; ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mpg";  ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".mpeg"; ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".3gp";  ValueData: "{#MyProgId}"
Root: HKLM; Subkey: "Software\{#MyAppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".ogv";  ValueData: "{#MyProgId}"

; OpenWithProgids — makes the app appear under "Open with" for each type.
Root: HKLM; Subkey: "Software\Classes\.mp4\OpenWithProgids";  ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.mkv\OpenWithProgids";  ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.avi\OpenWithProgids";  ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.mov\OpenWithProgids";  ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.wmv\OpenWithProgids";  ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.flv\OpenWithProgids";  ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.webm\OpenWithProgids"; ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.m4v\OpenWithProgids";  ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.ts\OpenWithProgids";   ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.m2ts\OpenWithProgids"; ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.mpg\OpenWithProgids";  ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.mpeg\OpenWithProgids"; ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.3gp\OpenWithProgids";  ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\.ogv\OpenWithProgids";  ValueType: none; ValueName: "{#MyProgId}"; Flags: uninsdeletevalue
