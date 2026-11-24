; The Windows installer for players who don't use the launcher: Inno Setup
; (jrsoftware.org, free), built by tools/build_package.py --installer from
; the package folder it has just staged:
;   iscc /DAppVersion=<version> /DDist=<package folder> /DStem=<file stem> /O<out dir> tools\package\installer.iss
; It installs to Program Files\Inception Rebuild with a Start-menu shortcut
; (a desktop one ticked by default) and an uninstaller. The game writes
; nothing there: settings.ini, the copy of the game the first start sets up
; (install\), the imported text, the saves and the log live in
; %APPDATA%\BattleTech\Inception Rebuild, which the uninstaller and every
; update leave alone. No file of the original game is in the installer.
; The installer and the program carry the project's icon (ICONS.md).

#ifndef AppVersion
  #define AppVersion "dev"
#endif
#ifndef Dist
  #error Give /DDist=<the package folder> (build_package.py does).
#endif
#ifndef Stem
  #define Stem "CHI-Rebuild-" + AppVersion
#endif

[Setup]
; This project's own AppId, the same every version: a new installer upgrades
; the old install in place.
AppId={{11AFD380-EF35-4002-81BC-7312A6971E0F}
AppName=Inception Rebuild
AppVersion={#AppVersion}
AppVerName=Inception Rebuild {#AppVersion}
AppPublisher=Inception Rebuild (a fan project)
AppComments=A rebuild of BattleTech: The Crescent Hawk's Inception; it needs your own copy of the DOS game, version 1.03.
DefaultDirName={autopf}\Inception Rebuild
DisableDirPage=no
DefaultGroupName=Inception Rebuild
DisableProgramGroupPage=yes
; Program Files needs administrator rights; a player without them can pick
; "only for me" (the user's own Programs folder) in the first dialog.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\chi.exe
UninstallDisplayName=Inception Rebuild
OutputBaseFilename={#Stem}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=chi.ico
#if FileExists(AddBackslash(Dist) + "LICENSE")
LicenseFile={#Dist}\LICENSE
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Dist}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; data\ is ours and replaced whole, so a file an earlier version shipped and
; this one dropped doesn't linger. Nothing else in {app} is touched.
Type: filesandordirs; Name: "{app}\data"

[Icons]
Name: "{autoprograms}\Inception Rebuild"; Filename: "{app}\chi.exe"; WorkingDir: "{app}"; Comment: "The Crescent Hawk's Inception, rebuilt"
Name: "{autoprograms}\Inception Rebuild - Read me"; Filename: "{app}\README.txt"
Name: "{autodesktop}\Inception Rebuild"; Filename: "{app}\chi.exe"; WorkingDir: "{app}"; Comment: "The Crescent Hawk's Inception, rebuilt"; Tasks: desktopicon

[Run]
Filename: "{app}\chi.exe"; Description: "{cm:LaunchProgram,Inception Rebuild}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Only what the installer put in {app}; the player's folder in %APPDATA% stays.
Type: filesandordirs; Name: "{app}\data"
Type: dirifempty; Name: "{app}"

[Messages]
UninstalledAll=%1 was removed from your computer.%n%nYour settings, saved games and the copy of the game it set up are kept in %APPDATA%\BattleTech\Inception Rebuild. Delete that folder to remove them too. Your own copy of the game was never changed.
