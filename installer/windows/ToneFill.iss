; Inno Setup script for ToneFill (Windows VST3, ARA-enabled).
; Compiled by the GitHub Actions workflow (.github/workflows/build-windows.yml):
;   ISCC.exe /DAppVersion=1.0.0 installer\windows\ToneFill.iss
; The workflow stages the built bundle into installer\windows\stage\ToneFill.vst3 first.

#ifndef AppVersion
  #define AppVersion "0.9"
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
DefaultDirName={commoncf64}\VST3
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
WelcomeLabel2=This will install {#AppName} {#AppVersion} (VST3, with ARA) on your computer.%n%nThe plugin is installed to the shared VST3 folder so every host can find it.

[Files]
; VST3 bundle (folder) -> C:\Program Files\Common Files\VST3\ToneFill.vst3
Source: "stage\ToneFill.vst3\*"; DestDir: "{commoncf64}\VST3\ToneFill.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs
; Manual next to the plugin docs.
Source: "stage\manual.html"; DestDir: "{commonpf64}\ToneFill"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\ToneFill Manual"; Filename: "{commonpf64}\ToneFill\manual.html"

[Run]
Filename: "{commonpf64}\ToneFill\manual.html"; Description: "Open the ToneFill manual"; \
    Flags: postinstall shellexec skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\ToneFill.vst3"
