param(
    [string]$Source = "src\metalbar.asm",
    [string]$Output = "bin\metalbar.exe",
    [switch]$FetchFasm
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$fasmDir = Join-Path $root "tools\fasm"
$fasm = Join-Path $fasmDir "FASM.EXE"

if (-not (Test-Path $fasm)) {
    $onPath = Get-Command fasm.exe -ErrorAction SilentlyContinue
    if ($onPath) {
        $fasm = $onPath.Source
        $fasmDir = Split-Path -Parent $fasm
    } elseif ($FetchFasm) {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        New-Item -ItemType Directory -Force -Path $fasmDir | Out-Null
        $zip = Join-Path $fasmDir "fasmw17335.zip"
        Write-Host "Fetching official FASM 1.73.35 from flatassembler.net..." -ForegroundColor Cyan
        Invoke-WebRequest -Uri "https://flatassembler.net/fasmw17335.zip" -OutFile $zip -UseBasicParsing
        Expand-Archive -Path $zip -DestinationPath $fasmDir -Force
        Remove-Item $zip -Force
    } else {
        throw "FASM.EXE not found. Run '.\build.ps1 -FetchFasm' or install FASM 1.73.35 to tools\fasm\."
    }
}

$srcPath = Join-Path $root $Source
$outPath = Join-Path $root $Output
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null

$incDir = Join-Path $fasmDir "INCLUDE"
if (Test-Path $incDir) { $env:INCLUDE = $incDir }

& $fasm $srcPath $outPath
if ($LASTEXITCODE -ne 0) {
    throw "FASM failed with exit code $LASTEXITCODE"
}

$sz = (Get-Item $outPath).Length
Write-Host ("Built {0} ({1:N0} bytes / {2:F2} KiB)" -f $outPath, $sz, ($sz / 1024.0)) -ForegroundColor Green
