# Configura e compila o OpenSpeak nativo.
#   .\build.ps1                 # Release completo (servidor + cliente + testes)
#   .\build.ps1 -ServerOnly     # só servidor e testes (sem FFmpeg; relay SFU se houver libdatachannel)
#   .\build.ps1 -Config Debug
#   .\build.ps1 -Tests 'tst_engine|ui_'   # só os testes cujo nome casa com a regex (ctest -R)
#   .\build.ps1 -Changed        # só os testes afetados desde a última tag v* (dia a dia)
# Sem -Tests/-Changed roda a suíte completa (é o que o package.ps1 usa nas releases).
param(
    [string]$Config = 'Release',
    [string]$Version = '1.0.28',
    [switch]$ServerOnly,
    [string]$Tests,
    [switch]$Changed,
    [string]$QtDir = 'C:\Qt\6.11.3\msvc2022_64',
    [string]$Vcpkg = 'D:\Dev\vcpkg'
)
# Avisos do CMake vão para stderr; não trate como erro (PowerShell 5.1).
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot

# Escolhe a regex do ctest a partir dos arquivos alterados desde a última tag v*
# (commits + working tree + não rastreados). '' = suíte completa; $null = pular o ctest.
function Get-ChangedTestFilter {
    $root = (& git -C $here rev-parse --show-toplevel 2>$null)
    $tag = (& git -C $here describe --tags --abbrev=0 --match 'v*' 2>$null)
    if ($LASTEXITCODE -or -not $root -or -not $tag) { Write-Host '-Changed: sem tag v* ou git; rodando todos os testes'; return '' }
    $files = @(& git -C $root diff --name-only "$tag..HEAD") + @(& git -C $root diff --name-only HEAD) +
        @(& git -C $root ls-files --others --exclude-standard)
    $files = @($files | Where-Object { $_ } | Sort-Object -Unique)
    $filters = [System.Collections.Generic.List[string]]::new()
    foreach ($f in $files) {
        # Fora de native/ (web, monitor, bot, docs/) não afeta o build nativo.
        if ($f -notmatch '^native/') { continue }
        # Dentro do switch, "continue" só encerra o switch (equivale a ir ao próximo arquivo).
        switch -Regex ($f) {
            '^native/installer/|\.md$' { continue }
            '^native/client/qml/' { $filters.Add('ui_'); continue }
            '^native/server/' { $filters.Add('tst_virtualserver|tst_sfu_e2e|tst_p2p_e2e'); continue }
            '^native/client/(core|rtc|video|media)/' { $filters.Add('tst_engine|tst_p2p_e2e|tst_cloudconnection'); continue }
            # Na dúvida (CMake, common/, testes, outros), roda tudo.
            default { Write-Host "-Changed: $f afeta tudo; rodando todos os testes"; return '' }
        }
    }
    if (-not $filters.Count) { Write-Host "-Changed: nada que exija testes desde $tag; pulando o ctest"; return $null }
    return (($filters -join '|') -split '\|' | Sort-Object -Unique) -join '|'
}

$filter = ''
if ($Tests) { $filter = $Tests }
elseif ($Changed) { $filter = Get-ChangedTestFilter }

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'

$client = if ($ServerOnly) { 'OFF' } else { 'ON' }
& $cmake -S $here -B "$here\build" -G 'Visual Studio 18 2026' -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtDir" `
    "-DCMAKE_TOOLCHAIN_FILE=$Vcpkg\scripts\buildsystems\vcpkg.cmake" `
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md' `
    "-DVCPKG_INSTALLED_DIR=$here\vcpkg_installed" `
    '-DVCPKG_MANIFEST_INSTALL=OFF' `
    '-DCMAKE_SUPPRESS_REGENERATION=ON' `
    "-DOPENSPEAK_BUILD_CLIENT=$client" `
    "-DOPENSPEAK_RELEASE_VERSION=$Version"
if ($LASTEXITCODE) { exit $LASTEXITCODE }

& $cmake --build "$here\build" --config $Config --parallel $env:NUMBER_OF_PROCESSORS
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if ($null -eq $filter) { exit 0 }
$env:PATH = "$QtDir\bin;$env:PATH"
$ctestArgs = @('--test-dir', "$here\build", '-C', $Config, '--output-on-failure')
if ($filter) {
    Write-Host "ctest -R '$filter'"
    $ctestArgs += @('-R', $filter)
}
& $ctest @ctestArgs
exit $LASTEXITCODE
