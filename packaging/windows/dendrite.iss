; dendrite Windows installer (Inno Setup 6). Built by CI:
;   iscc /DAppVersion=2.5.0 /DSourceDir=<staging dir> /O<out dir> packaging\windows\dendrite.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist"
#endif

[Setup]
; AppId never changes: Windows recognises updates by it.
AppId={{6B0E2C61-5D0A-4C7E-9E11-3F2D7A1C9D57}
AppName=dendrite
AppVersion={#AppVersion}
AppVerName=dendrite {#AppVersion}
AppPublisher=afferent
AppPublisherURL=https://afferent.page
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
Uninstallable=yes
UninstallFilesDir={commonappdata}\dendrite\uninstall
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=dendrite-{#AppVersion}-windows-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
InfoBeforeFile={#SourceDir}\CREDITS.txt
CloseApplications=yes

[InstallDelete]
; dendrite's old name: two copies confuse Live
Type: filesandordirs; Name: "{commoncf64}\VST3\viber.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\dendrite.vst3"

[Files]
Source: "{#SourceDir}\dendrite.vst3\*"; DestDir: "{commoncf64}\VST3\dendrite.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#SourceDir}\dendrite-manual.pdf"; DestDir: "{commonappdata}\dendrite"; Flags: ignoreversion
Source: "{#SourceDir}\CREDITS.txt"; DestDir: "{commonappdata}\dendrite"; Flags: ignoreversion

[Run]
Filename: "{commonappdata}\dendrite\dendrite-manual.pdf"; Description: "Open the manual"; Flags: postinstall shellexec skipifsilent unchecked
