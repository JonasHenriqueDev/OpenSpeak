param([Parameter(Mandatory=$true)][string]$ServerUrl)
$ErrorActionPreference = 'Stop'
$uri = [uri]$ServerUrl
if ($uri.Scheme -ne 'wss' -or -not $uri.Host.EndsWith('.ts.net') -or $uri.UserInfo) { throw 'Informe um servidor wss://...ts.net/ws' }
$repo = Split-Path $PSScriptRoot
$dest = Join-Path $repo 'web-dist'
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item -Path (Join-Path $repo 'public\*') -Destination $dest -Recurse -Force
$config = @{serverUrl=$ServerUrl; tailscaleOnly=$true} | ConvertTo-Json -Compress
Set-Content -LiteralPath (Join-Path $dest 'config.js') -Value "window.OpenSpeakConfig = $config;" -Encoding UTF8
Write-Output "Cliente web pronto em $dest"
