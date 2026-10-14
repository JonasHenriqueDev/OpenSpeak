; Instalador do OpenSpeak (Inno Setup 6).
; Gerado por native/package.ps1 — que monta native/dist/OpenSpeak antes.

#ifndef AppVersion
  #define AppVersion "1.0.3"
#endif
#ifndef Edition
  #define Edition "lite"
#endif
#define AppName "OpenSpeak"
#define AppExe "OpenSpeak.exe"
#ifndef Staging
  #if Edition == "full"
    #define Staging "..\dist\OpenSpeak"
  #else
    #define Staging "..\dist\lite\OpenSpeak"
  #endif
#endif
; Mesmo AppId nas duas edições: trocar de edição atualiza a instalação existente.
#if Edition == "full"
  #define SetupName "OpenSpeak-Full-Setup"
  #define AppVerName "OpenSpeak " + AppVersion
#else
  #define SetupName "OpenSpeak-Setup"
  #define AppVerName "OpenSpeak Lite " + AppVersion
#endif
#ifndef AppId
  #define AppId "{{6F1B4F0A-0E2C-4C1B-9E7A-0F5E2B1C9D11}"
#endif

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppVerName}
AppPublisher=OpenSpeak
AppPublisherURL=https://github.com/JonasHenriqueDev/OpenSpeak
DefaultDirName={localappdata}\Programs\{#AppName}
UsePreviousAppDir=no
DisableDirPage=yes
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename={#SetupName}-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
WizardStyle=modern
SetupIconFile=..\client\assets\openspeak.ico
DisableProgramGroupPage=yes
CloseApplications=yes

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Staging}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

#if Edition != "full"
[InstallDelete]
; Ao trocar da edição completa para a lite, remove o que só a completa instala.
Type: files; Name: "{app}\openspeak-server.exe"
Type: files; Name: "{app}\Qt6HttpServer.dll"
Type: filesandordirs; Name: "{app}\public"
Type: files; Name: "{group}\Servidor dedicado OpenSpeak.lnk"
#endif

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
#if Edition == "full"
Name: "{group}\Servidor dedicado OpenSpeak"; Filename: "{app}\openspeak-server.exe"; Parameters: "--port 9988"
#endif
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; Per-user setup does not modify machine-wide firewall rules.
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\{#AppExe}"; Flags: nowait runasoriginaluser; Check: RelaunchAfterUpdate

[Code]
function RelaunchAfterUpdate(): Boolean;
begin
  Result := (ExpandConstant('{param:UPDATE|0}') = '1') and
            (ExpandConstant('{param:RELAUNCH|0}') = '1');
end;

// Tailscale é opcional, oferecido apenas nos modos avançados do app.
