; Instalador do OpenSpeak (Inno Setup 6).
; Gerado por native/package.ps1 — que monta native/dist/OpenSpeak antes.

#ifndef AppVersion
  #define AppVersion "0.2.0"
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

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""OpenSpeak"""; Flags: runhidden; RunOnceId: "fw1"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""OpenSpeak Server"""; Flags: runhidden; RunOnceId: "fw2"

[Code]
// O único requisito externo é o Tailscale: avisa se não estiver instalado.
function TailscaleInstalled(): Boolean;
begin
  Result := FileExists(ExpandConstant('{commonpf64}\Tailscale\tailscale.exe'))
         or FileExists(ExpandConstant('{commonpf32}\Tailscale\tailscale.exe'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ErrorCode: Integer;
begin
  if (CurStep = ssPostInstall) and (not TailscaleInstalled()) and (not WizardSilent()) then
  begin
    if MsgBox('O OpenSpeak usa o Tailscale para encontrar as pessoas da sua rede (sem servidor).' + #13#10 +
              'O Tailscale não foi encontrado. Deseja abrir a página de download agora?',
              mbConfirmation, MB_YESNO) = IDYES then
      ShellExec('open', 'https://tailscale.com/download/windows', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
  end;
end;
