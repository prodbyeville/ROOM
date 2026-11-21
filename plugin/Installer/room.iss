; Windows installer for ROOM (Inno Setup 6).
; Built by CI:  iscc /DAppVersion=1.0.0 /DSourceDir=<dir with ROOM.vst3 + INSTALL.txt> /O<out> room.iss
; Puts ROOM.vst3 in the standard VST3 folder and adds an uninstaller.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\package"
#endif

[Setup]
AppId={{6E3C8A41-2B7D-4F0E-9C55-6A1D8E0B7F21}
AppName=ROOM
AppVersion={#AppVersion}
AppVerName=ROOM {#AppVersion}
AppPublisher=Eville
AppPublisherURL=https://prodbyeville.com
AppSupportURL=https://prodbyeville.com
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DefaultGroupName=ROOM
DisableProgramGroupPage=yes
UninstallFilesDir={commonpf64}\Eville\ROOM
UninstallDisplayName=ROOM {#AppVersion}
UninstallDisplayIcon={commonpf64}\Eville\ROOM\unins000.exe
OutputBaseFilename=ROOM-{#AppVersion}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=no
SetupIconFile=assets\room.ico

; The look: ROOM's black, the red art on the Welcome and Finish pages, the red mark top right
; (assets made by make-assets.py).
WizardStyle=modern dark hidebevels
WizardSizePercent=110
WizardBackColor=#0a0a0a
WizardImageFile=assets\wizard.png,assets\wizard@2x.png
WizardSmallImageFile=assets\small.png,assets\small@2x.png
WizardImageStretch=yes

[Messages]
WelcomeLabel1=Welcome to ROOM
WelcomeLabel2=This puts ROOM {#AppVersion} (VST3) in your VST3 folder.%n%nClose your DAW before you go on.
FinishedHeadingLabel=ROOM is installed
FinishedLabelNoIcons=ROOM {#AppVersion} is in your VST3 folder.%n%nOpen your DAW (rescan plugins if it doesn't show up), put ROOM on a track and sign in with your email and the license key from your ROOM email.
FinishedLabel=ROOM {#AppVersion} is in your VST3 folder.%n%nOpen your DAW (rescan plugins if it doesn't show up), put ROOM on a track and sign in with your email and the license key from your ROOM email.

[Files]
Source: "{#SourceDir}\ROOM.vst3\*"; DestDir: "{commoncf64}\VST3\ROOM.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\INSTALL.txt"; DestDir: "{commonpf64}\Eville\ROOM"; DestName: "ROOM notes.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\ROOM notes"; Filename: "{commonpf64}\Eville\ROOM\ROOM notes.txt"
Name: "{group}\Uninstall ROOM"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\ROOM.vst3"
