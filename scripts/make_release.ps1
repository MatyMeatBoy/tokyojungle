# Tokyo Jungle Recompiled -- release zip for GitHub Releases and RecompLauncher.
#
#   powershell -File scripts\make_release.ps1 -Version v0.1.4
#
# Makes dist\TokyoJungle-Recompiled-<version>-Windows-x64.zip with, at the top:
#   BUILD.bat              the automatic build (unchanged)
#   TokyoJungle.exe        the launcher, compiled from tools\launcher\launcher.c
#   recomplauncher.json    how RecompLauncher sets the game up and starts it
#   LICENSE
# No game data: the game itself is built on the player's PC from their own copy.
# Also copies BUILD.bat to dist\ for the README's download link.
param(
    [Parameter(Mandatory = $true)][string]$Version
)
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).ProviderPath
$Dist = Join-Path $Repo 'dist'
$Stage = Join-Path $Dist 'stage'
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

# MSVC headers and libraries for clang-cl.
if (-not $env:VCINSTALLDIR) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = if (Test-Path $vswhere) {
        & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $vs) { $vs = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools" }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found (Visual Studio C++ tools): $vcvars" }
    $env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"   # vcvars calls vswhere
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^(.*?)=(.*)$') { Set-Item "env:$($matches[1])" $matches[2] }
    }
}
$clang = (Get-Command clang-cl -ErrorAction SilentlyContinue).Source
if (-not $clang) { $clang = "$env:ProgramFiles\LLVM\bin\clang-cl.exe" }

# Same command as scripts\tj_build_all.ps1 uses for the launcher.
& $clang /O2 /nologo /MT /utf-8 -D_CRT_SECURE_NO_WARNINGS (Join-Path $Repo 'tools\launcher\launcher.c') `
    "/Fe:$Stage\TokyoJungle.exe" "/Fo:$Stage\launcher.obj" /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib
if ($LASTEXITCODE) { throw "clang-cl failed ($LASTEXITCODE)" }
Remove-Item (Join-Path $Stage 'launcher.obj')

foreach ($f in 'BUILD.bat', 'recomplauncher.json', 'LICENSE') { Copy-Item (Join-Path $Repo $f) $Stage }
Get-Content -Raw (Join-Path $Stage 'recomplauncher.json') | ConvertFrom-Json | Out-Null   # must parse

$zip = Join-Path $Dist "TokyoJungle-Recompiled-$Version-Windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $zip
Copy-Item (Join-Path $Repo 'BUILD.bat') $Dist -Force
Remove-Item -Recurse -Force $Stage
Write-Host "Made $zip"
