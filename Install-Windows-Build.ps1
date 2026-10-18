param([string]$ZipPath = (Join-Path $HOME 'Downloads\EasyBank-Windows-x64.zip'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
foreach ($file in @('EasyBank.cpp','sqlite3.c','Run-Local.ps1')) {
    if (!(Test-Path (Join-Path $root $file))) { throw "Wrong project directory. Missing $file" }
}
if (!(Test-Path -LiteralPath $ZipPath)) { throw "Build artifact not found: $ZipPath" }
$temp = Join-Path $env:TEMP ('EasyBank-artifact-' + [guid]::NewGuid().ToString('N'))
$bin = Join-Path $root 'bin'
$target = Join-Path $bin 'EasyBank.exe'
$backup = Join-Path $bin ('EasyBank.exe.backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$hadOld = Test-Path -LiteralPath $target
try {
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $temp
    $files = @(Get-ChildItem -LiteralPath $temp -Recurse -File)
    if ($files.Count -ne 1 -or $files[0].Name -ne 'EasyBank.exe') {
        throw 'Unexpected artifact contents. Expected exactly one EasyBank.exe.'
    }
    New-Item -ItemType Directory -Force -Path $bin | Out-Null
    if ($hadOld) { Copy-Item -LiteralPath $target -Destination $backup }
    Copy-Item -LiteralPath $files[0].FullName -Destination $target -Force
    (Get-Item -LiteralPath $target).LastWriteTimeUtc = [DateTime]::UtcNow
    & $target --self-test
    if ($LASTEXITCODE -ne 0) { throw "Downloaded executable self-test failed (exit code $LASTEXITCODE)" }
    Write-Host 'EASYBANK_WINDOWS_EXECUTABLE=READY' -ForegroundColor Green
    Write-Host 'Now run: powershell -NoProfile -ExecutionPolicy Bypass -File .\Run-Local.ps1'
} catch {
    if ($hadOld -and (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $backup -Destination $target -Force }
    elseif (-not $hadOld -and (Test-Path -LiteralPath $target)) { Remove-Item -LiteralPath $target -Force }
    throw
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
