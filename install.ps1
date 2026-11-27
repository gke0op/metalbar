# ============================================================================
# MetalBar — Bare-Metal Windows 11 Avionics Installer
#   irm https://raw.githubusercontent.com/gke0op/metalbar/main/install.ps1 | iex
#   curl.exe -fsSL https://raw.githubusercontent.com/gke0op/metalbar/main/install.ps1 | powershell -NoProfile -Command -
# ============================================================================
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$e   = [char]27
$RST = "$e[0m"
$DIM = "$e[38;5;244m"
$BDR = "$e[38;5;68m"
$CYN = "$e[38;5;45m"
$WHT = "$e[38;5;255m"
$AMB = "$e[38;5;214m"
$GRN = "$e[38;5;84m"
$RED = "$e[38;5;203m"

$Repo       = "gke0op/metalbar"
$InstallDir = Join-Path $env:LOCALAPPDATA "MetalBar"
$ExePath    = Join-Path $InstallDir "metalbar.exe"
$ApiUrl     = "https://api.github.com/repos/$Repo/releases/latest"

function Write-Row([string]$left, [string]$color = $WHT) {
    $plain = ($left -replace '\x1b\[[0-9;]*m', '')
    $pad   = [Math]::Max(0, 70 - $plain.Length)
    Write-Host ("$BDR|$RST  $color$left$RST" + (' ' * $pad) + "$BDR|$RST")
}

Write-Host ""
Write-Host "$BDR+========================================================================+$RST"
Write-Row ("${CYN}METALBAR$RST ${DIM}//$RST ${WHT}BARE-METAL WINDOWS 11 AVIONICS$RST   ${AMB}[x86-64 FASM | 4 DLL]$RST")
Write-Host "$BDR+========================================================================+$RST"

# 1. Query latest GitHub release
Write-Row ("${DIM}[1/4]$RST ${WHT}TELEM   $RST ${DIM}Querying github.com/$Repo/releases/latest...$RST")
$headers = @{ "User-Agent" = "MetalBar-Installer" }
$rel = Invoke-RestMethod -Uri $ApiUrl -Headers $headers
$tag = $rel.tag_name
$exeAsset = $rel.assets | Where-Object { $_.name -eq "metalbar.exe" } | Select-Object -First 1
$sumAsset = $rel.assets | Where-Object { $_.name -eq "SHA256SUMS.txt" } | Select-Object -First 1
if (-not $exeAsset) { throw "Release $tag is missing metalbar.exe asset." }

# 2. Download binary & checksum
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
$tmpExe = "$ExePath.tmp"
$tmpSum = Join-Path $InstallDir "SHA256SUMS.txt"

Write-Row ("${DIM}[2/4]$RST ${WHT}FETCH   $RST ${CYN}$tag$RST ${DIM}->$RST ${WHT}metalbar.exe$RST")
Invoke-WebRequest -Uri $exeAsset.browser_download_url -OutFile $tmpExe -UseBasicParsing -Headers $headers
$sz = (Get-Item $tmpExe).Length
$kib = [Math]::Round($sz / 1024.0, 2)
Write-Row ("        ${CYN}[########################################]$RST ${WHT}$sz B$RST ${DIM}($kib KiB)$RST")

# 3. Verify SHA-256 + PE64 x86-64 header
$shaStatus = "${AMB}SKIP$RST"
$actualHash = (Get-FileHash -Path $tmpExe -Algorithm SHA256).Hash.ToLower()
if ($sumAsset) {
    Invoke-WebRequest -Uri $sumAsset.browser_download_url -OutFile $tmpSum -UseBasicParsing -Headers $headers
    $sumLine = (Get-Content $tmpSum | Where-Object { $_ -match 'metalbar\.exe' } | Select-Object -First 1)
    Remove-Item $tmpSum -Force -ErrorAction SilentlyContinue
    if ($sumLine) {
        $expectedHash = ($sumLine.Trim() -split '\s+')[0].ToLower()
        if ($actualHash -ne $expectedHash) {
            Remove-Item $tmpExe -Force -ErrorAction SilentlyContinue
            Write-Row ("${RED}[FAIL]$RST  SHA-256 MISMATCH! Aborting install.")
            Write-Host "$BDR+========================================================================+$RST"
            throw "SHA-256 verification failed! Expected $expectedHash, got $actualHash"
        }
        $shaStatus = "${GRN}OK$RST"
    }
}

$bytes = [IO.File]::ReadAllBytes($tmpExe)
$peOff = [BitConverter]::ToInt32($bytes, 0x3C)
$isPe64 = ($bytes[0] -eq 0x4D -and $bytes[1] -eq 0x5A -and [BitConverter]::ToUInt16($bytes, $peOff + 4) -eq 0x8664)
if (-not $isPe64) {
    Remove-Item $tmpExe -Force -ErrorAction SilentlyContinue
    throw "Downloaded binary is not a valid PE64 x86-64 executable."
}
$shortSha = $actualHash.Substring(0, 12)
Write-Row ("${DIM}[3/4]$RST ${WHT}VERIFY  $RST SHA256:${DIM}$shortSha$RST [$shaStatus]  PE64:[${GRN}OK$RST]  ZERO-CRT:[${GRN}OK$RST]")

# 4. Hot-swap binary cleanly
$wasRunning = Get-Process -Name metalbar -ErrorAction SilentlyContinue
if ($wasRunning) {
    Stop-Process -Name metalbar -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 600
}
Move-Item -Path $tmpExe -Destination $ExePath -Force
$dispPath = $ExePath.Replace($env:LOCALAPPDATA, "%LOCALAPPDATA%")
Write-Row ("${DIM}[4/4]$RST ${WHT}INSTALL $RST ${CYN}$dispPath$RST")
Write-Host "$BDR+------------------------------------------------------------------------+$RST"

if ($env:METALBAR_NO_LAUNCH -ne '1') {
    Start-Process -FilePath $ExePath
    Write-Row ("${GRN}ONLINE$RST  ${WHT}CTRL+SHIFT+SPACE$RST ${DIM}HUD$RST  ${WHT}SPACE$RST ${DIM}BLACKBOX$RST  ${WHT}CTRL+SHF+C$RST ${DIM}THEMES$RST")
} else {
    Write-Row ("${GRN}READY $RST  Run: ${CYN}$dispPath$RST")
}
Write-Row ("        ${WHT}F$RST ${DIM}FREEZE$RST  ${WHT}L$RST ${DIM}LOW-GEAR$RST  ${WHT}K$RST ${DIM}KILL$RST  ${WHT}TAB$RST ${DIM}CTRL/SET$RST  ${WHT}CTRL+SHF+Q$RST ${DIM}QUIT$RST")
Write-Host "$BDR+========================================================================+$RST"
Write-Host ""
