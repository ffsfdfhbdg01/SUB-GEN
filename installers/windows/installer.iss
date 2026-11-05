; SPDX-License-Identifier: AGPL-3.0-or-later
; The Windows installer (SPEC_BUILD_RELEASE §4.2), Inno Setup 6: the VST3 bundle into Common Files\VST3. Built as
;   ISCC /DAppVersion=<VERSION> /DVst3Dir=<the built SUB-GEN.vst3> /DOutDir=<dist> installers\windows\installer.iss
; and installed silently with /VERYSILENT /SUPPRESSMSGBOXES /NORESTART. Unsigned (SmartScreen: "More info", "Run
; anyway"). Factory presets join as a component with Phase 4. It shows the display name, SAM SUB GEN
; (SUBGEN_DISPLAY_NAME in cmake/ProductIdentity.cmake); files and paths keep SUB-GEN.

#ifndef AppVersion
  #error AppVersion is required (the VERSION file)
#endif
#ifndef Vst3Dir
  #error Vst3Dir is required (the built SUB-GEN.vst3 folder)
#endif
#ifndef OutDir
  #define OutDir "."
#endif

[Setup]
AppId={{FE593D89-6024-4EB4-AA31-9E6BB0FF450A}
AppName=SAM SUB GEN
AppVersion={#AppVersion}
AppVerName=SAM SUB GEN {#AppVersion}
AppPublisher=Vendor
VersionInfoVersion={#AppVersion}
; {app} is the shared VST3 folder; the uninstaller lives under ProgramData, not among the plug-ins hosts scan
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallFilesDir={commonappdata}\Vendor\SUB-GEN
UninstallDisplayName=SAM SUB GEN {#AppVersion}
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\..\LICENSE
OutputDir={#OutDir}
OutputBaseFilename=SUB-GEN-{#AppVersion}-Windows
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Types]
Name: "full"; Description: "VST3"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "SAM SUB GEN VST3 (Common Files\VST3)"; Types: full custom; Flags: fixed

[Files]
Source: "{#Vst3Dir}\*"; DestDir: "{commoncf64}\VST3\SUB-GEN.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\SUB-GEN.vst3"
Type: dirifempty; Name: "{commonappdata}\Vendor\SUB-GEN"
Type: dirifempty; Name: "{commonappdata}\Vendor"
