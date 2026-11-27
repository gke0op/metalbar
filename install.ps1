# ============================================================================
# MetalBar - Windows 11 x86-64 Installer
#   irm https://raw.githubusercontent.com/gke0op/metalbar/main/install.ps1 | iex
#   curl.exe -fsSL https://raw.githubusercontent.com/gke0op/metalbar/main/install.ps1 | powershell -NoProfile -Command "$input | Out-String | iex"
# ============================================================================
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$_mbInv  = [Globalization.CultureInfo]::InvariantCulture
$_mbEsc  = [char]27
$_mbRst  = "$_mbEsc[0m"
$_mbHide = "$_mbEsc[?25l"
$_mbShow = "$_mbEsc[?25h"

function _mbFg([int]$r, [int]$g, [int]$b) { return "$_mbEsc[38;2;$r;$g;${b}m" }
function _mbPad([string]$txt, [int]$width) {
    $plain = ($txt -replace '\x1b\[[0-9;]*m', '')
    $pad = [Math]::Max(0, $width - $plain.Length)
    return $txt + (' ' * $pad)
}
function _mbBar([int]$filled, [int]$total, [bool]$done) {
    $sb = New-Object Text.StringBuilder
    [void]$sb.Append("${_mbDim}[")
    for ($i = 0; $i -lt $total; $i++) {
        if ($i -lt $filled) {
            $t = $i / [double][Math]::Max(1, $total - 1)
            $r = [int](116 + (56 - 116) * $t)
            $g = [int](172 + (212 - 172) * $t)
            $b = [int](232 + (255 - 232) * $t)
            if (-not $done -and $i -eq ($filled - 1)) {
                [void]$sb.Append("${_mbWht}>")
            } else {
                [void]$sb.Append((_mbFg $r $g $b)).Append('=')
            }
        } else {
            [void]$sb.Append("${_mbRule}.")
        }
    }
    [void]$sb.Append("${_mbDim}]")
    return $sb.ToString()
}
function _mbPe64([string]$path) {
    $b = [IO.File]::ReadAllBytes($path)
    if ($b.Length -lt 256 -or $b[0] -ne 0x4D -or $b[1] -ne 0x5A) { return $null }
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    if ($pe -lt 0 -or ($pe + 24 + 128) -gt $b.Length) { return $null }
    if ($b[$pe] -ne 0x50 -or $b[$pe + 1] -ne 0x45 -or $b[$pe + 2] -ne 0 -or $b[$pe + 3] -ne 0) { return $null }
    if ([BitConverter]::ToUInt16($b, $pe + 4) -ne 0x8664) { return $null }
    if ([BitConverter]::ToUInt16($b, $pe + 24) -ne 0x020B) { return $null }
    $nSec = [BitConverter]::ToUInt16($b, $pe + 6)
    $optSz = [BitConverter]::ToUInt16($b, $pe + 20)
    $secOff = $pe + 24 + $optSz
    $secs = @{}
    $secArr = @()
    for ($i = 0; $i -lt $nSec; $i++) {
        $o = $secOff + $i * 40
        if (($o + 40) -gt $b.Length) { break }
        $nm = [Text.Encoding]::ASCII.GetString($b, $o, 8).Trim([char]0)
        $vsz = [BitConverter]::ToUInt32($b, $o + 8)
        $va  = [BitConverter]::ToUInt32($b, $o + 12)
        $rsz = [BitConverter]::ToUInt32($b, $o + 16)
        $raw = [BitConverter]::ToUInt32($b, $o + 20)
        $secs[$nm] = @{ VSz = $vsz; RSz = $rsz }
        $secArr += @{ VA = $va; VSz = $vsz; RSz = $rsz; Raw = $raw }
    }
    $impRva = [BitConverter]::ToUInt32($b, $pe + 24 + 120)
    $impOff = -1
    foreach ($s in $secArr) {
        if ($impRva -ge $s.VA -and $impRva -lt ($s.VA + [Math]::Max($s.VSz, $s.RSz))) {
            $impOff = $s.Raw + ($impRva - $s.VA); break
        }
    }
    $dlls = @()
    while ($impOff -ge 0 -and ($impOff + 20) -le $b.Length) {
        $nr = [BitConverter]::ToUInt32($b, $impOff + 12)
        if ($nr -eq 0) { break }
        $no = -1
        foreach ($s in $secArr) {
            if ($nr -ge $s.VA -and $nr -lt ($s.VA + [Math]::Max($s.VSz, $s.RSz))) {
                $no = $s.Raw + ($nr - $s.VA); break
            }
        }
        if ($no -lt 0 -or $no -ge $b.Length) { break }
        $end = $no
        while ($end -lt $b.Length -and $b[$end] -ne 0) { $end++ }
        $dlls += [Text.Encoding]::ASCII.GetString($b, $no, $end - $no)
        $impOff += 20
    }
    return @{ Size = $b.Length; Secs = $secs; Dlls = $dlls }
}

$_mbCyn  = _mbFg 56 212 255
$_mbBlu  = _mbFg 116 172 232
$_mbDim  = _mbFg 96 112 132
$_mbRule = _mbFg 58 84 114
$_mbWht  = _mbFg 238 244 252
$_mbGrn  = _mbFg 66 255 130
$_mbAmb  = _mbFg 255 184 48
$_mbRed  = _mbFg 255 75 75
$_mbW    = 72

$_mbRepo       = "gke0op/metalbar"
$_mbInstallDir = Join-Path $env:LOCALAPPDATA "MetalBar"
$_mbExePath    = Join-Path $_mbInstallDir "metalbar.exe"
$_mbApiUrl     = "https://api.github.com/repos/$_mbRepo/releases/latest"
$_mbHeaders    = @{ "User-Agent" = "MetalBar-Installer" }

$_mbRel = Invoke-RestMethod -Uri $_mbApiUrl -Headers $_mbHeaders
$_mbTag = $_mbRel.tag_name
$_mbExeAsset = $_mbRel.assets | Where-Object { $_.name -eq "metalbar.exe" } | Select-Object -First 1
$_mbSumAsset = $_mbRel.assets | Where-Object { $_.name -eq "SHA256SUMS.txt" } | Select-Object -First 1
if (-not $_mbExeAsset) { throw "Release $_mbTag is missing metalbar.exe asset." }

$_mbTotalBytes = [long]$_mbExeAsset.size
$_mbKibStr = [string]::Format($_mbInv, "{0:F2} KiB", ($_mbTotalBytes / 1024.0))

New-Item -ItemType Directory -Force -Path $_mbInstallDir | Out-Null
$_mbTmpExe = "$_mbExePath.tmp"
$_mbTmpSum = Join-Path $_mbInstallDir "SHA256SUMS.txt"
$_mbDispPath = $_mbExePath.Replace($env:LOCALAPPDATA, "%LOCALAPPDATA%")
$_mbIsTty = (-not [Console]::IsOutputRedirected) -or ($env:METALBAR_FORCE_TTY -eq '1')

Write-Host ""
$_mbW0 = " __  __   ____  _____   ___    _      ____    ___   ____  "
$_mbW1 = "|  \/  | | __| |_   _| / _ \  | |    | __ )  / _ \ |  _ \ "
$_mbW2 = "|_|\/|_| |____|  |_|  /_/ \_\ |____| |____) /_/ \_\|_| \_\"
Write-Host ("${_mbCyn}$_mbW0${_mbWht}" + ($_mbTag.PadLeft($_mbW - $_mbW0.Length)) + $_mbRst)
Write-Host ("${_mbBlu}$_mbW1${_mbDim}" + ("x86-64 FASM".PadLeft($_mbW - $_mbW1.Length)) + $_mbRst)
Write-Host ("${_mbBlu}$_mbW2${_mbDim}" + ($_mbKibStr.PadLeft($_mbW - $_mbW2.Length)) + $_mbRst)
Write-Host ("${_mbRule}" + ('-' * $_mbW) + $_mbRst)

$_mbDrawn = $false
$_mbDynRows = 7
function _mbRender([hashtable]$st) {
    if ($_mbDrawn) { Write-Host -NoNewline "$_mbEsc[${_mbDynRows}A" }
    $bar  = _mbBar $st.BarN 42 $st.FetchDone
    $gotS = [string]::Format($_mbInv, "{0,6:N0} B", $st.GotB)
    $pctS = [string]::Format($_mbInv, "{0,3}%", $st.Pct)

    $l1 = (_mbPad "${_mbBlu}SHA256  $($st.ShaTxt)" 32) + $st.ShaSt
    $l2 = (_mbPad "${_mbBlu}PE64    $($st.PeTxt)"  32) + $st.PeSt
    $l3 = (_mbPad "${_mbBlu}IAT     $($st.IatTxt)" 32) + $st.IatSt

    $r1 = "${_mbDim}.text  ${_mbWht}$($st.TextSz)   ${_mbDim}.rdata  ${_mbWht}$($st.RdataSz)"
    $r2 = "${_mbDim}.data  ${_mbWht}$($st.DataSz)   ${_mbDim}.bss   ${_mbWht}$($st.BssMb)"
    $r3 = "${_mbDim}HEAP   ${_mbWht}$($st.HeapSz)   ${_mbDim}CRT    ${_mbWht}$($st.CrtSz)"

    $lines = @(
        "${_mbBlu}FETCH   $bar ${_mbWht}$gotS ${_mbCyn}$pctS  $($st.FetchSt)$_mbRst"
        "$l1 ${_mbRule}|$_mbRst $r1$_mbRst"
        "$l2 ${_mbRule}|$_mbRst $r2$_mbRst"
        "$l3 ${_mbRule}|$_mbRst $r3$_mbRst"
        "${_mbRule}" + ('-' * $_mbW) + $_mbRst
        "$($st.InstLine)$_mbRst"
        (_mbPad $st.FootLine $_mbW) + $_mbRst
    )
    Write-Host ($lines -join "`n")
    $script:_mbDrawn = $true
}

$_mbSt = @{
    BarN = 0; FetchDone = $false; GotB = 0; Pct = 0; FetchSt = "${_mbAmb}[..]"
    ShaTxt = "${_mbDim}pending...      "; ShaSt = "${_mbDim}[  ]"
    PeTxt  = "${_mbDim}pending...      "; PeSt  = "${_mbDim}[  ]"
    IatTxt = "${_mbDim}pending...      "; IatSt = "${_mbDim}[  ]"
    TextSz = "     -- "; RdataSz = "    -- "; DataSz = "     -- "; BssMb = "      --"
    HeapSz = "     -- "; CrtSz   = "      --"
    InstLine = (_mbPad "${_mbBlu}INSTALL ${_mbDim}downloading binary..." 68) + "${_mbDim}[  ]"
    FootLine = "${_mbBlu}KEYS    ${_mbWht}CLICK BAR ${_mbDim}-> HUD | ${_mbWht}SPACE ${_mbDim}5M | ${_mbWht}F/L/K ${_mbDim}PROC | ${_mbWht}CTRL+SHF+C ${_mbDim}THEME"
}

if ($_mbIsTty) { Write-Host -NoNewline $_mbHide }
try {
    if ($_mbIsTty) { _mbRender $_mbSt }

    # Stream download in 2 KiB chunks to drive the single filling bar from live wire bytes
    $_mbReq = [Net.HttpWebRequest]::Create($_mbExeAsset.browser_download_url)
    $_mbReq.UserAgent = "MetalBar-Installer"
    $_mbReq.AllowAutoRedirect = $true
    $_mbResp = $_mbReq.GetResponse()
    try {
        if ($_mbResp.ContentLength -gt 0) { $_mbTotalBytes = $_mbResp.ContentLength }
        $_mbIn = $_mbResp.GetResponseStream()
        $_mbOut = [IO.File]::Create($_mbTmpExe)
        try {
            $_mbBuf = New-Object byte[] 2048
            $_mbGot = [long]0
            while ($true) {
                $_mbRead = $_mbIn.Read($_mbBuf, 0, $_mbBuf.Length)
                if ($_mbRead -le 0) { break }
                $_mbOut.Write($_mbBuf, 0, $_mbRead)
                $_mbGot += $_mbRead
                $_mbPct = if ($_mbTotalBytes -gt 0) { [Math]::Min(100, [int]($_mbGot * 100 / $_mbTotalBytes)) } else { 50 }
                $_mbNewBar = [Math]::Min(42, [int](42 * $_mbPct / 100))
                $_mbSt.GotB = $_mbGot
                $_mbSt.Pct  = $_mbPct
                if ($_mbNewBar -gt $_mbSt.BarN) {
                    $_mbSt.BarN = $_mbNewBar
                    if ($_mbIsTty) {
                        _mbRender $_mbSt
                        Start-Sleep -Milliseconds 12
                    }
                }
            }
        } finally {
            $_mbOut.Dispose()
            $_mbIn.Dispose()
        }
    } finally {
        $_mbResp.Close()
    }

    $_mbSz = (Get-Item $_mbTmpExe).Length
    $_mbKibStr = [string]::Format($_mbInv, "{0:F2} KiB", ($_mbSz / 1024.0))
    $_mbSt.GotB = $_mbSz; $_mbSt.Pct = 100; $_mbSt.BarN = 42; $_mbSt.FetchDone = $true; $_mbSt.FetchSt = "${_mbGrn}[OK]"
    $_mbSt.ShaTxt = "${_mbAmb}hashing...      "
    if ($_mbIsTty) { _mbRender $_mbSt }

    # Real SHA-256 verification against release SHA256SUMS.txt
    $_mbSha = (Get-FileHash -Path $_mbTmpExe -Algorithm SHA256).Hash.ToLowerInvariant()
    $_mbShaOk = $false
    $_mbShaSt = "${_mbAmb}[SK]"
    if ($_mbSumAsset) {
        Invoke-WebRequest -Uri $_mbSumAsset.browser_download_url -OutFile $_mbTmpSum -UseBasicParsing -Headers $_mbHeaders
        $_mbSumLine = (Get-Content $_mbTmpSum | Where-Object { $_ -match 'metalbar\.exe' } | Select-Object -First 1)
        Remove-Item $_mbTmpSum -Force -ErrorAction SilentlyContinue
        if ($_mbSumLine) {
            $_mbExp = ($_mbSumLine.Trim() -split '\s+')[0].ToLowerInvariant()
            if ($_mbSha -ne $_mbExp) {
                Remove-Item $_mbTmpExe -Force -ErrorAction SilentlyContinue
                $_mbSt.ShaTxt = "${_mbRed}$($_mbSha.Substring(0,16))"
                $_mbSt.ShaSt  = "${_mbRed}[!!]"
                $_mbSt.InstLine = (_mbPad "${_mbRed}ABORT   SHA-256 MISMATCH" 68) + "${_mbRed}[!!]"
                _mbRender $_mbSt
                throw "SHA-256 verification failed. Expected $_mbExp, got $_mbSha"
            }
            $_mbShaOk = $true
            $_mbShaSt = "${_mbGrn}[OK]"
        }
    }
    $_mbSt.ShaTxt = "${_mbWht}$($_mbSha.Substring(0,16))"
    $_mbSt.ShaSt  = $_mbShaSt
    $_mbSt.PeTxt  = "${_mbAmb}parsing PE64... "
    if ($_mbIsTty) { _mbRender $_mbSt; Start-Sleep -Milliseconds 35 }

    # Real PE64 + section + .idata static import inspection
    $_mbPe = _mbPe64 $_mbTmpExe
    if (-not $_mbPe) {
        Remove-Item $_mbTmpExe -Force -ErrorAction SilentlyContinue
        $_mbSt.PeTxt = "${_mbRed}INVALID PE64"
        $_mbSt.PeSt  = "${_mbRed}[!!]"
        _mbRender $_mbSt
        throw "Downloaded binary is not a valid PE64 x86-64 executable."
    }

    $_mbShortDlls = ($_mbPe.Dlls | ForEach-Object {
        switch ($_.ToLowerInvariant()) {
            'ntdll.dll'    { 'ntdll' }
            'kernel32.dll' { 'k32' }
            'user32.dll'   { 'u32' }
            'gdi32.dll'    { 'gdi32' }
            default        { $_ }
        }
    }) -join ' '

    $_mbTextV  = if ($_mbPe.Secs.ContainsKey('.text'))  { $_mbPe.Secs['.text'].VSz }  else { 0 }
    $_mbRdataV = if ($_mbPe.Secs.ContainsKey('.rdata')) { $_mbPe.Secs['.rdata'].VSz } else { 0 }
    $_mbDataV  = if ($_mbPe.Secs.ContainsKey('.data'))  { $_mbPe.Secs['.data'].VSz }  else { 0 }
    $_mbBssV   = if ($_mbPe.Secs.ContainsKey('.bss'))   { $_mbPe.Secs['.bss'].VSz }   else { 0 }

    $_mbSt.PeTxt   = "${_mbWht}AMD64 / $($_mbPe.Dlls.Count) STATIC DLLS"
    $_mbSt.PeSt    = "${_mbGrn}[OK]"
    $_mbSt.IatTxt  = "${_mbWht}$_mbShortDlls"
    $_mbSt.IatSt   = "${_mbGrn}[OK]"
    $_mbSt.TextSz  = [string]::Format($_mbInv, "{0,6:N0} B", $_mbTextV)
    $_mbSt.RdataSz = [string]::Format($_mbInv, "{0,5:N0} B", $_mbRdataV)
    $_mbSt.DataSz  = [string]::Format($_mbInv, "{0,6:N0} B", $_mbDataV)
    $_mbSt.BssMb   = [string]::Format($_mbInv, "{0,4:F2} MiB", ($_mbBssV / 1MB))
    $_mbSt.HeapSz  = "     0 B"
    $_mbSt.CrtSz   = "     0 B"
    $_mbSt.InstLine = (_mbPad "${_mbBlu}INSTALL ${_mbAmb}swapping binary..." 68) + "${_mbAmb}[..]"
    if ($_mbIsTty) { _mbRender $_mbSt; Start-Sleep -Milliseconds 35 }

    # Atomic swap (stop running instance cleanly if present)
    $_mbRunning = Get-Process -Name metalbar -ErrorAction SilentlyContinue
    if ($_mbRunning) {
        Stop-Process -Name metalbar -Force -ErrorAction SilentlyContinue
        for ($_mbW8 = 0; $_mbW8 -lt 20; $_mbW8++) {
            if (-not (Get-Process -Name metalbar -ErrorAction SilentlyContinue)) { break }
            Start-Sleep -Milliseconds 50
        }
    }
    Move-Item -Path $_mbTmpExe -Destination $_mbExePath -Force

    if ($env:METALBAR_NO_LAUNCH -ne '1') {
        Start-Process -FilePath $_mbExePath
    } else {
        $_mbSt.FootLine = "${_mbGrn}READY   ${_mbWht}$_mbDispPath ${_mbDim}(NO_LAUNCH=1)"
    }
    $_mbSt.InstLine = (_mbPad "${_mbBlu}INSTALL ${_mbWht}$_mbDispPath ${_mbDim}($_mbKibStr)" 68) + "${_mbGrn}[OK]"
    _mbRender $_mbSt
} finally {
    if ($_mbIsTty) { Write-Host -NoNewline $_mbShow }
}
Write-Host ""
