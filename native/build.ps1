# Configura e compila o OpenSpeak nativo.
#   .\build.ps1                 # Release completo (servidor + cliente + testes)
#   .\build.ps1 -ServerOnly     # só servidor e testes (sem FFmpeg; relay SFU se houver libdatachannel)
#   .\build.ps1 -Config Debug
param(
    [string]$Config = 'Release',
    [string]$Version = '1.0.7',
    [switch]$ServerOnly,
    [string]$QtDir = 'C:\Qt\6.11.3\msvc2022_64',
    [string]$Vcpkg = 'D:\Dev\vcpkg'
)
# Avisos do CMake vão para stderr; não trate como erro (PowerShell 5.1).
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot

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

& $cmake --build "$here\build" --config $Config --parallel 2
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$env:PATH = "$QtDir\bin;$env:PATH"
& $ctest --test-dir "$here\build" -C $Config --output-on-failure
exit $LASTEXITCODE
