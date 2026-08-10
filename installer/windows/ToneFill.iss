; Inno Setup script for ToneFill (Windows VST3 + AAX).
; Compiled by the GitHub Actions workflow (.github/workflows/build-windows.yml):
;   ISCC.exe /DAppVersion=1.0.0 installer\windows\ToneFill.iss
; The workflow stages:
;   installer\windows\stage\ToneFill.vst3       (built by CI)
;   installer\windows\stage\ToneFill.aaxplugin  (signed AAX, downloaded from the release; optional)
; When the AAX bundle isn't staged, the AAX files are skipped and a VST3-only installer is built.

#ifndef AppVersion
  #define AppVersion "0.9.4"
#endif
#define AppName "ToneFill"
#define AppPublisher "cactuzz sound"
#define AppURL "https://github.com/cactuzzsound/tonefill-ara"

[Setup]
AppId={{7F3B2C10-9A4E-4E77-9C2E-0A1B2C3D4E5F}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
DefaultDirName={commoncf64}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=Output
OutputBaseFilename=ToneFill-{#AppVersion}-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName} {#AppVersion}

[Messages]
WelcomeLabel2=This will install {#AppName} {#AppVersion} on your computer.%n%nChoose the plug-in formats to install. VST3 (with ARA) goes to the shared VST3 folder; AAX goes to the Pro Tools plug-ins folder.

[Types]
Name: "full";   Description: "VST3 + AAX (recommended)"
Name: "custom"; Description: "Choose formats"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (with ARA) - Nuendo, Cubase, Reaper, ..."; Types: full custom
Name: "aax";  Description: "AAX plug-in - Pro Tools";                               Types: full custom

[Files]
; VST3 bundle (folder) -> C:\Program Files\Common Files\VST3\ToneFill.vst3
Source: "stage\ToneFill.vst3\*"; DestDir: "{commoncf64}\VST3\ToneFill.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
; AAX bundle (folder) -> C:\Program Files\Common Files\Avid\Audio\Plug-Ins\ToneFill.aaxplugin
; skipifsourcedoesntexist: build a VST3-only installer when the signed AAX wasn't staged.
Source: "stage\ToneFill.aaxplugin\*"; DestDir: "{commoncf64}\Avid\Audio\Plug-Ins\ToneFill.aaxplugin"; \
    Components: aax; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
; Manual next to the plugin docs.
Source: "stage\manual.html"; DestDir: "{commonpf64}\ToneFill"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\ToneFill Manual"; Filename: "{commonpf64}\ToneFill\manual.html"

[Run]
Filename: "{commonpf64}\ToneFill\manual.html"; Description: "Open the ToneFill manual"; \
    Flags: postinstall shellexec skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\ToneFill.vst3"
Type: filesandordirs; Name: "{commoncf64}\Avid\Audio\Plug-Ins\ToneFill.aaxplugin"
