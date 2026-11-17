; dendrite Windows installer (Inno Setup 6). Built by CI:
;   iscc /DAppVersion=2.5.0 /DSourceDir=<staging dir> /O<out dir> packaging\windows\dendrite.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist"
#endif
; the pictures and icon: tools/installer/make_art.py draws them
#define Art "art"

[Setup]
; AppId never changes: Windows recognises updates by it.
AppId={{6B0E2C61-5D0A-4C7E-9E11-3F2D7A1C9D57}
AppName=dendrite
AppVersion={#AppVersion}
AppVerName=dendrite {#AppVersion}
AppPublisher=hakdiriko
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
WizardSizePercent=110
WizardImageFile={#Art}\wizard-100.bmp,{#Art}\wizard-125.bmp,{#Art}\wizard-150.bmp,{#Art}\wizard-175.bmp,{#Art}\wizard-200.bmp,{#Art}\wizard-225.bmp,{#Art}\wizard-250.bmp
WizardSmallImageFile={#Art}\small-100.bmp,{#Art}\small-125.bmp,{#Art}\small-150.bmp,{#Art}\small-175.bmp,{#Art}\small-200.bmp,{#Art}\small-225.bmp,{#Art}\small-250.bmp
WizardImageStretch=no
WizardImageBackColor=$000000
SetupIconFile={#Art}\dendrite.ico
UninstallDisplayIcon={commonappdata}\dendrite\dendrite.ico
UninstallDisplayName=dendrite {#AppVersion} (VST3)
VersionInfoVersion={#AppVersion}
VersionInfoCompany=hakdiriko
VersionInfoDescription=dendrite VST3 installer
VersionInfoProductName=dendrite
VersionInfoProductVersion={#AppVersion}
VersionInfoCopyright=(c) 2026 hakdiriko
AppCopyright=(c) 2026 hakdiriko
AppSupportURL=https://afferent.page
AppUpdatesURL=https://afferent.page
ShowLanguageDialog=no
CloseApplications=yes

[InstallDelete]
; dendrite's old name: two copies confuse Live
Type: filesandordirs; Name: "{commoncf64}\VST3\viber.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\dendrite.vst3"

[Files]
Source: "{#SourceDir}\dendrite.vst3\*"; DestDir: "{commoncf64}\VST3\dendrite.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#SourceDir}\dendrite-manual.pdf"; DestDir: "{commonappdata}\dendrite"; Flags: ignoreversion
Source: "{#SourceDir}\CREDITS.txt"; DestDir: "{commonappdata}\dendrite"; Flags: ignoreversion
Source: "{#Art}\dendrite.ico"; DestDir: "{commonappdata}\dendrite"; Flags: ignoreversion

[Run]
Filename: "{commonappdata}\dendrite\dendrite-manual.pdf"; Description: "Open the manual"; Flags: postinstall shellexec skipifsilent unchecked

[Messages]
WelcomeLabel1=dendrite {#AppVersion}
WelcomeLabel2=A dub echo that grows around your head.%n%nThis puts the dendrite VST3 into your system plug-in folder, where Ableton Live and every other VST3 host find it. Please close your DAW before you go on.
FinishedHeadingLabel=dendrite is in place
FinishedLabelNoIcons=Open your DAW and rescan plug-ins. In Ableton Live: Settings, Plug-Ins, turn on "Use VST3 Plug-In System Folders", Rescan. Then find it under Plug-Ins, VST3, hakdiriko.
FinishedLabel=Open your DAW and rescan plug-ins. In Ableton Live: Settings, Plug-Ins, turn on "Use VST3 Plug-In System Folders", Rescan. Then find it under Plug-Ins, VST3, hakdiriko.
ClickFinish=
