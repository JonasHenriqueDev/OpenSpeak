; Instalador do OpenSpeak (Inno Setup 6).
; Gerado por native/package.ps1 — que monta native/dist/OpenSpeak antes.

#ifndef AppVersion
  #define AppVersion "1.0.1"
#endif
#define AppName "OpenSpeak"
#define AppExe "OpenSpeak.exe"
#define Staging "..\dist\OpenSpeak"

[Setup]
AppId={{6F1B4F0A-0E2C-4C1B-9E7A-0F5E2B1C9D11}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=OpenSpeak
AppPublisherURL=https://github.com/JonasHenriqueDev/OpenSpeak
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=OpenSpeak-Setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
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

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Servidor dedicado OpenSpeak"; Filename: "{app}\openspeak-server.exe"; Parameters: "--port 9988"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; Libera o app no firewall: malha (TCP 9987, só na tailnet) + mídia WebRTC (UDP).
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""OpenSpeak"" dir=in action=allow program=""{app}\{#AppExe}"" enable=yes profile=any"; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""OpenSpeak Server"" dir=in action=allow program=""{app}\openspeak-server.exe"" enable=yes profile=any"; Flags: runhidden
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\{#AppExe}"; Flags: nowait runasoriginaluser; Check: RelaunchAfterUpdate

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""OpenSpeak"""; Flags: runhidden; RunOnceId: "fw1"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""OpenSpeak Server"""; Flags: runhidden; RunOnceId: "fw2"

[Code]
function RelaunchAfterUpdate(): Boolean;
begin
  Result := (ExpandConstant('{param:UPDATE|0}') = '1') and
            (ExpandConstant('{param:RELAUNCH|0}') = '1');
end;

// Tailscale é opcional, oferecido apenas nos modos avançados do app.
