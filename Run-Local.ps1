param([switch]$TestOnly)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$source = Join-Path $root 'EasyBank.cpp'
$sqlite = Join-Path $root 'sqlite3.c'
$exe = Join-Path $root 'bin\EasyBank.exe'

if (-not (Test-Path -LiteralPath $source) -or -not (Test-Path -LiteralPath $sqlite)) {
    throw 'EasyBank source is incomplete.'
}

$rebuild = -not (Test-Path -LiteralPath $exe)
if (-not $rebuild) {
    $outputDate = (Get-Item -LiteralPath $exe).LastWriteTimeUtc
    $rebuild = (Get-Item -LiteralPath $source).LastWriteTimeUtc -gt $outputDate -or
               (Get-Item -LiteralPath $sqlite).LastWriteTimeUtc -gt $outputDate
}

if ($rebuild) {
    Write-Host 'Building EasyBank using the existing Windows C++ compiler...' -ForegroundColor Cyan
    $developer = $null
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        $vswhere = if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe' } else { '' }
        if ($vswhere -and (Test-Path -LiteralPath $vswhere)) {
            $install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($install) {
                $candidate = Join-Path ($install | Select-Object -First 1) 'Common7\Tools\VsDevCmd.bat'
                if (Test-Path -LiteralPath $candidate) { $developer = $candidate }
            }
        }
        if (-not $developer) {
            foreach ($year in @('2026','2022','2019')) {
                foreach ($edition in @('Community','Professional','Enterprise','BuildTools')) {
                    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
                        if (-not $base) { continue }
                        $candidate = Join-Path $base "Microsoft Visual Studio\$year\$edition\Common7\Tools\VsDevCmd.bat"
                        if (Test-Path -LiteralPath $candidate) { $developer = $candidate; break }
                    }
                    if ($developer) { break }
                }
                if ($developer) { break }
            }
        }
        if (-not $developer) {
            throw 'MSVC C++ compiler not found. The original project needs Visual Studio C++ Desktop tools; no packages were installed.'
        }
    }

    New-Item -ItemType Directory -Force -Path (Join-Path $root 'bin') | Out-Null
    $build = Join-Path $root 'bin\build-local.cmd'
    $commands = @('@echo off','setlocal')
    if ($developer) { $commands += 'call "' + $developer + '" -arch=x64 -host_arch=x64 >nul || exit /b 1' }
    $commands += 'cl /nologo /std:c++17 /EHsc /Od /W3 /utf-8 /I"' + $root + '" /Fe:"' + $exe + '" "' + $source + '" "' + $sqlite + '" bcrypt.lib || exit /b 1'
    $commands += 'exit /b 0'
    Set-Content -LiteralPath $build -Value $commands -Encoding Ascii
    & cmd.exe /d /c ('"' + $build + '"')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exe)) {
        throw 'C++ build failed. Please share the complete compiler output.'
    }
}

Write-Host 'Verifying core banking workflows...' -ForegroundColor Cyan
& $exe --self-test
if ($LASTEXITCODE -ne 0) { throw 'Local self-test failed. Existing data was not modified by the test.' }
if (-not $TestOnly) {
    Write-Host 'Opening EasyBank local console...' -ForegroundColor Green
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "EasyBank exited with code $LASTEXITCODE" }
}
