; dendrite Windows installer (Inno Setup 6). Built by CI:
;   iscc /DAppVersion=2.5.0 /DSourceDir=<staging dir> /O<out dir> packaging\windows\dendrite.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist"
#endif
; the pictures and icon: tools/installer/make_art.py draws them from the fly's atlas
#define Art "art"

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
InfoBeforeFile={#SourceDir}\CREDITS.txt
CloseApplications=yes
ShowLanguageDialog=no
; the rig's look: black, greys, Fragment Mono, the fly at its atlas places
; (dark mode needs Inno Setup 6.6, a black page 6.7; CI installs the latest)
#if VER >= EncodeVer(6,7,0)
WizardStyle=modern dark includetitlebar hidebevels
WizardBackColor=#000000
#else
WizardStyle=modern
#endif
WizardSizePercent=120
WizardImageFile={#Art}\wizard-100.bmp,{#Art}\wizard-125.bmp,{#Art}\wizard-150.bmp,{#Art}\wizard-175.bmp,{#Art}\wizard-200.bmp,{#Art}\wizard-225.bmp,{#Art}\wizard-250.bmp
WizardSmallImageFile={#Art}\small-100.bmp,{#Art}\small-125.bmp,{#Art}\small-150.bmp,{#Art}\small-175.bmp,{#Art}\small-200.bmp,{#Art}\small-225.bmp,{#Art}\small-250.bmp
WizardImageStretch=no
WizardImageBackColor=$000000
SetupIconFile={#Art}\dendrite.ico
UninstallDisplayIcon={commonappdata}\dendrite\dendrite.ico
UninstallDisplayName=dendrite {#AppVersion} (VST3)
VersionInfoVersion={#AppVersion}
VersionInfoCompany=afferent
VersionInfoDescription=dendrite VST3 installer
VersionInfoProductName=dendrite
VersionInfoProductVersion={#AppVersion}
AppCopyright=(c) 2026 afferent

[LangOptions]
; Fragment Mono, the window's face, registered for this setup only (see [Code])
DialogFontName=Fragment Mono
DialogFontSize=9
WelcomeFontName=Fragment Mono
WelcomeFontSize=13
TitleFontName=Fragment Mono

[InstallDelete]
; dendrite's old name: two copies confuse Live
Type: filesandordirs; Name: "{commoncf64}\VST3\viber.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\dendrite.vst3"

[Files]
Source: "{#SourceDir}\dendrite.vst3\*"; DestDir: "{commoncf64}\VST3\dendrite.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#SourceDir}\dendrite-manual.pdf"; DestDir: "{commonappdata}\dendrite"; Flags: ignoreversion
Source: "{#SourceDir}\CREDITS.txt"; DestDir: "{commonappdata}\dendrite"; Flags: ignoreversion
Source: "{#Art}\dendrite.ico"; DestDir: "{commonappdata}\dendrite"; Flags: ignoreversion
Source: "..\..\Resources\fonts\FragmentMono-Regular.ttf"; Flags: dontcopy

[Run]
Filename: "{commonappdata}\dendrite\dendrite-manual.pdf"; Description: "open the manual"; Flags: postinstall shellexec skipifsilent unchecked

[Messages]
SetupWindowTitle=setup · dendrite {#AppVersion}
WelcomeLabel1=dendrite {#AppVersion}
WelcomeLabel2=a dub echo that grows around your head.%n%nthis puts the dendrite vst3 into your system plug-in folder, where ableton live and every other vst3 host find it.%n%nclose your daw before you go on.
WizardInfoBefore=credits
InfoBeforeMessage=the fly, the fonts, the code it stands on.
InfoBeforeClickLabel=next, when you have read it.
WizardReady=ready
ReadyLabel1=everything is in place to install dendrite.
ReadyLabel2b=install puts dendrite into C:\Program Files\Common Files\VST3.
WizardInstalling=installing
InstallingLabel=putting the specimen in place.
FinishedHeadingLabel=dendrite is in place
FinishedLabelNoIcons=open ableton live and rescan:%nsettings · plug-ins · use vst3 plug-in system folders · rescan%n%nthen find it under plug-ins · vst3 · afferent.
FinishedLabel=open ableton live and rescan:%nsettings · plug-ins · use vst3 plug-in system folders · rescan%n%nthen find it under plug-ins · vst3 · afferent.
ClickFinish=
ButtonNext=&next
ButtonBack=&back
ButtonCancel=cancel
ButtonInstall=&install
ButtonFinish=&finish

[Code]
// Fragment Mono for this setup only: it is unpacked to setup's own temp folder and registered
// privately, before the wizard is drawn. Nothing is installed into Windows' fonts.
function AddFontResourceEx(FileName: string; Flags: Cardinal; Reserved: Cardinal): Integer;
  external 'AddFontResourceExW@gdi32.dll stdcall';

function InitializeSetup(): Boolean;
begin
  ExtractTemporaryFile('FragmentMono-Regular.ttf');
  AddFontResourceEx(ExpandConstant('{tmp}\FragmentMono-Regular.ttf'), $10, 0);
  Result := True;
end;
