# Shared helpers for install.ps1 / uninstall.ps1 (Windows PowerShell 5.1 and later). Nothing is downloaded.
$ErrorActionPreference = 'Stop'
$AppId = '292030'
$StateDir = if ($env:W3VR_TEST_STATE) { $env:W3VR_TEST_STATE } else { Join-Path $env:LOCALAPPDATA 'Witcher3VR' }
$StateFile = Join-Path $StateDir 'install.json'
$Builds = @{ '6AC555A3-06402000' = '501'; '664FB0DC-0612E000' = '404' }

function Stop-WithError([string]$Message) {
    Write-Host ''
    Write-Host "Error: $Message" -ForegroundColor Red
    exit 1
}

function Get-SteamRoot {
    if ($env:W3VR_TEST_STEAM) { return $env:W3VR_TEST_STEAM }
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
        try {
            $props = Get-ItemProperty -Path $key -ErrorAction Stop
            foreach ($name in 'SteamPath', 'InstallPath') {
                $value = $props.$name
                if ($value -and (Test-Path -LiteralPath $value)) { return ($value -replace '/', '\') }
            }
        } catch { }
    }
    $default = Join-Path ${env:ProgramFiles(x86)} 'Steam'
    if (Test-Path -LiteralPath $default) { return $default }
    return $null
}

function Find-Game([string]$Steam) {
    $libraries = @($Steam)
    $vdf = Join-Path $Steam 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        $text = [IO.File]::ReadAllText($vdf)
        foreach ($m in [regex]::Matches($text, '"path"\s+"([^"]*)"')) { $libraries += ($m.Groups[1].Value -replace '\\\\', '\') }
    }
    foreach ($lib in ($libraries | Select-Object -Unique)) {
        $acf = Join-Path $lib "steamapps\appmanifest_$AppId.acf"
        if (-not (Test-Path -LiteralPath $acf)) { continue }
        $dir = 'The Witcher 3'
        $m = [regex]::Match([IO.File]::ReadAllText($acf), '"installdir"\s+"([^"]*)"')
        if ($m.Success) { $dir = $m.Groups[1].Value }
        $game = Join-Path $lib "steamapps\common\$dir"
        if (Test-Path -LiteralPath (Join-Path $game 'bin\x64_dx12')) { return $game }
    }
    return $null
}

function Get-DocsFolder {
    if ($env:W3VR_TEST_DOCS) { return $env:W3VR_TEST_DOCS }
    return (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'The Witcher 3')
}

function Get-ExeBuild([string]$Exe) {
    if (-not (Test-Path -LiteralPath $Exe)) { return $null }
    $buffer = New-Object byte[] 4096
    $stream = [IO.File]::OpenRead($Exe)
    try { [void]$stream.Read($buffer, 0, 4096) } finally { $stream.Close() }
    $pe = [BitConverter]::ToInt32($buffer, 0x3C)
    if ($pe -lt 0 -or $pe -gt 4000 -or $buffer[$pe] -ne 0x50 -or $buffer[$pe + 1] -ne 0x45) { return $null }
    $stamp = [BitConverter]::ToUInt32($buffer, $pe + 8)
    $size = [BitConverter]::ToUInt32($buffer, $pe + 24 + 56)
    return ('{0:X8}-{1:X8}' -f $stamp, $size)
}

function Test-GameRunning {
    return [bool](Get-Process -Name 'witcher3' -ErrorAction SilentlyContinue)
}

# ---- line-preserving ini editing (dx12user.settings, witcher3vr.ini)
function Read-Ini([string]$Path) {
    $text = [IO.File]::ReadAllText($Path)
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = New-Object 'System.Collections.Generic.List[string]'
    foreach ($line in ($text -replace "`r`n", "`n").Split("`n")) { $lines.Add($line) }
    if ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -eq '') { $lines.RemoveAt($lines.Count - 1) }
    return @{ Lines = $lines; NL = $nl; Path = $Path }
}

function Save-Ini($Ini) {
    $text = ($Ini.Lines -join $Ini.NL) + $Ini.NL
    $tmp = $Ini.Path + '.w3vr.tmp'
    [IO.File]::WriteAllText($tmp, $text, (New-Object Text.UTF8Encoding($false)))
    if (Test-Path -LiteralPath $Ini.Path) { Copy-Item -LiteralPath $Ini.Path -Destination ($Ini.Path + '.w3vr.bak') -Force }
    Move-Item -LiteralPath $tmp -Destination $Ini.Path -Force
}

function Get-IniBounds($Ini, [string]$Section) {
    $lines = $Ini.Lines
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $t = $lines[$i].Trim()
        if ($t.StartsWith('[') -and $t.EndsWith(']') -and $t.Substring(1, $t.Length - 2).Trim() -ieq $Section) {
            $end = $lines.Count
            for ($j = $i + 1; $j -lt $lines.Count; $j++) {
                $u = $lines[$j].Trim()
                if ($u.StartsWith('[') -and $u.EndsWith(']')) { $end = $j; break }
            }
            return @($i, $end)
        }
    }
    return $null
}

function Find-IniKeyLine($Ini, [string]$Section, [string]$Key) {
    $b = Get-IniBounds $Ini $Section
    if (-not $b) { return -1 }
    for ($i = $b[0] + 1; $i -lt $b[1]; $i++) {
        $line = $Ini.Lines[$i]
        if ($line.Contains('=') -and -not $line.TrimStart().StartsWith(';') -and -not $line.TrimStart().StartsWith('#')) {
            if ($line.Split('=', 2)[0].Trim() -ieq $Key) { return $i }
        }
    }
    return -1
}

function Get-IniValue($Ini, [string]$Section, [string]$Key) {
    $i = Find-IniKeyLine $Ini $Section $Key
    if ($i -lt 0) { return $null }
    return $Ini.Lines[$i].Split('=', 2)[1].Trim()
}

function Set-IniValue($Ini, [string]$Section, [string]$Key, $Value) {
    $i = Find-IniKeyLine $Ini $Section $Key
    if ($null -eq $Value) {
        if ($i -ge 0) { $Ini.Lines.RemoveAt($i) }
        return
    }
    if ($i -ge 0) { $Ini.Lines[$i] = "$Key=$Value"; return }
    $b = Get-IniBounds $Ini $Section
    if (-not $b) {
        if ($Ini.Lines.Count -gt 0 -and $Ini.Lines[$Ini.Lines.Count - 1].Trim()) { $Ini.Lines.Add('') }
        $Ini.Lines.Add("[$Section]")
        $Ini.Lines.Add("$Key=$Value")
        return
    }
    $insert = $b[1]
    while ($insert -gt $b[0] + 1 -and -not $Ini.Lines[$insert - 1].Trim()) { $insert-- }
    $Ini.Lines.Insert($insert, "$Key=$Value")
}

function Read-State {
    if (Test-Path -LiteralPath $StateFile) { return (Get-Content -Raw -LiteralPath $StateFile | ConvertFrom-Json) }
    return $null
}
