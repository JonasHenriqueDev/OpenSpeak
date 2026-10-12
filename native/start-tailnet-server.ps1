param(
    [string]$PortableExe = '',
    [int]$Port = 9988
)
$ErrorActionPreference = 'Stop'
if (-not $PortableExe) {
    $PortableExe = Join-Path $PSScriptRoot 'OpenSpeak-Portable-0.2.2.exe'
    if (-not (Test-Path -LiteralPath $PortableExe)) {
        $PortableExe = Join-Path $PSScriptRoot 'dist\OpenSpeak-Portable-0.2.2.exe'
    }
}
$tailscale = Join-Path $env:ProgramFiles 'Tailscale\tailscale.exe'
if (-not (Test-Path -LiteralPath $tailscale)) { throw 'Instale e conecte o Tailscale primeiro.' }
$status = & $tailscale status --json | ConvertFrom-Json
if ($status.BackendState -ne 'Running') { throw 'Conecte este PC à tailnet primeiro.' }
if (-not (Test-Path -LiteralPath $PortableExe)) { throw "Portátil não encontrado: $PortableExe" }
if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
    throw "A porta $Port já está em uso. Mantenha o servidor existente ou encerre-o antes de iniciar outro."
}
# No STUN/TURN: native clients bind media to their Tailscale adapter for ts.net servers.
$previousIce = $env:OPENSPEAK_ICE
try {
    $env:OPENSPEAK_ICE = '[]'
    $process = Start-Process -FilePath $PortableExe -ArgumentList '--server-only','--host','127.0.0.1','--port',"$Port" -WindowStyle Hidden -PassThru
} finally { $env:OPENSPEAK_ICE = $previousIce }
$deadline = (Get-Date).AddSeconds(45)
do {
    if ($process.HasExited) { throw 'O servidor portátil encerrou antes de abrir a porta.' }
    Start-Sleep -Milliseconds 250
    $ready = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
} until ($ready -or (Get-Date) -gt $deadline)
if (-not $ready) { throw 'O servidor não iniciou dentro de 45 segundos.' }
& $tailscale serve --bg "http://127.0.0.1:$Port"
if ($LASTEXITCODE) { throw 'Habilite HTTPS/Serve no painel do Tailscale e execute novamente apenas tailscale serve.' }
$dns = $status.Self.DNSName.TrimEnd('.')
Write-Output "Servidor iniciado. Processo portátil: $($process.Id)"
Write-Output "Web: https://$dns/"
Write-Output "No app local, selecione Servidor dedicado: wss://$dns/ws"
Write-Output 'Este PC precisa permanecer ligado. O servidor não foi registrado para iniciar com o Windows.'
