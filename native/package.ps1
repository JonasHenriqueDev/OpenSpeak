# Monta native/dist/OpenSpeak (autônomo) e gera o instalador OpenSpeak-Setup-<versão>.exe.
#   .\package.ps1                # compila Release antes
#   .\package.ps1 -SkipBuild
param(
    [string]$Version = '0.2.3',
    [switch]$SkipBuild,
    [string]$QtDir = 'C:\Qt\6.11.3\msvc2022_64'
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$release = Join-Path $here 'build\Release'
$staging = Join-Path $here 'dist\OpenSpeak'

if (-not $SkipBuild) {
    & (Join-Path $here 'build.ps1')
    if ($LASTEXITCODE) { Write-Error 'build/testes falharam'; exit 1 }
}

# 1) Executáveis + dependências do Qt (windeployqt)
if (Test-Path $staging) {
    $resolvedStaging = (Resolve-Path -LiteralPath $staging).Path
    if ($resolvedStaging -ne [System.IO.Path]::GetFullPath((Join-Path $here 'dist\OpenSpeak'))) { throw 'Diretório de staging inesperado' }
    Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
}
New-Item -ItemType Directory -Force $staging | Out-Null
Copy-Item (Join-Path $release 'OpenSpeak.exe'), (Join-Path $release 'openspeak-server.exe') $staging
$deploy = Join-Path $QtDir 'bin\windeployqt.exe'
& $deploy --release --no-translations --no-system-d3d-compiler --qmldir (Join-Path $here 'client\qml') (Join-Path $staging 'OpenSpeak.exe') | Out-Null
if ($LASTEXITCODE) { throw 'windeployqt do cliente falhou' }
& $deploy --release --no-translations (Join-Path $staging 'openspeak-server.exe') | Out-Null
if ($LASTEXITCODE) { throw 'windeployqt do servidor falhou' }

# 2) Runtime do Visual C++ ao lado do .exe (dispensa o vc_redist)
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory | Sort-Object Name -Descending |
    ForEach-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } | Select-Object -First 1
Copy-Item (Join-Path $crt.FullName '*.dll') $staging

# 3) Cliente web (servido pelo servidor dedicado)
Copy-Item -Recurse (Join-Path $here '..\public') (Join-Path $staging 'public')

# 4) Instalador
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { Write-Error 'Inno Setup 6 não encontrado (winget install JRSoftware.InnoSetup)'; exit 1 }
& $iscc "/DAppVersion=$Version" (Join-Path $here 'installer\OpenSpeak.iss')
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# 5) Distribuição portátil com as mesmas dependências do instalador.
Compress-Archive -Path $staging -DestinationPath (Join-Path $here "dist\OpenSpeak-$Version-portable.zip") -Force

# 6) Um único exe: launcher nativo com o ZIP embutido e runtime estático.
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S (Join-Path $here 'portable') -B (Join-Path $here 'build\portable') -G 'Visual Studio 18 2026' -A x64 "-DPAYLOAD_ZIP=$here/dist/OpenSpeak-$Version-portable.zip"
if ($LASTEXITCODE) { throw 'Configuração do launcher portátil falhou' }
& $cmake --build (Join-Path $here 'build\portable') --config Release --parallel
if ($LASTEXITCODE) { throw 'Build do launcher portátil falhou' }
Copy-Item (Join-Path $here 'build\portable\Release\OpenSpeakPortable.exe') (Join-Path $here "dist\OpenSpeak-Portable-$Version.exe") -Force
exit 0
