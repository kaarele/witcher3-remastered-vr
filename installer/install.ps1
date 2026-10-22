# Witcher 3 Remastered VR - Windows installer, run by "Install (Windows).bat".
# Everything comes from the release folder next to this script; nothing is downloaded.
# Steps: find the game, back up saves, copy the mod files, set the VR graphics settings, write a manifest for
# "Uninstall (Windows)". Saves are only copied, never changed.
. (Join-Path $PSScriptRoot 'common.ps1')

$Root = Split-Path -Parent $PSScriptRoot
$Payload = Join-Path $Root 'payload'
Write-Host 'Witcher 3 Remastered VR - installer'
Write-Host '-----------------------------------'
if (-not (Test-Path -LiteralPath (Join-Path $Payload 'bin\x64_dx12\dxgi.dll'))) {
    Stop-WithError 'The release files are missing next to this installer. Extract the whole zip first, then run "Install (Windows)" from the extracted folder.'
}
$steam = Get-SteamRoot
if (-not $steam) { Stop-WithError 'Steam was not found.' }
$game = Find-Game $steam
if (-not $game) { Stop-WithError 'The Witcher 3 was not found in your Steam libraries. Install it in Steam first.' }
Write-Host "Found the game: $game"
if (Test-GameRunning) { Stop-WithError 'The Witcher 3 is running. Quit the game, then run the installer again.' }
$x64 = Join-Path $game 'bin\x64_dx12'
$exeId = Get-ExeBuild (Join-Path $x64 'witcher3.exe')
$build = $null
if ($exeId -and $Builds.ContainsKey($exeId)) { $build = $Builds[$exeId] }
if ($build -ne '501') {
    if ($build -eq '404') {
        Stop-WithError 'This is the Next-Gen 4.04 version. This mod is made for Remastered 5.01 (Steam''s default version). Nothing was changed.'
    }
    Stop-WithError 'This game version is not supported yet; the mod would stay inactive. A mod update for this version is needed. Nothing was changed.'
}
Write-Host 'Game version: Remastered 5.01 (supported)'

$docs = Get-DocsFolder
$saves = Join-Path $docs 'gamesaves'
if (Test-Path -LiteralPath $saves) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backup = Join-Path $env:USERPROFILE "w3vr-backups\$stamp"
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    Copy-Item -LiteralPath $saves -Destination $backup -Recurse -Force
    foreach ($file in Get-ChildItem -LiteralPath $saves -Recurse -File) {
        $copy = Join-Path (Join-Path $backup 'gamesaves') $file.FullName.Substring($saves.Length).TrimStart('\')
        if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $copy).Hash) {
            Stop-WithError "Save backup check failed for $($file.Name). Nothing else was changed."
        }
    }
    Write-Host "Saves backed up to $backup"
}

$state = Read-State
$files = @{}
$backups = @{}
$changes = @{}
if ($state -and $state.game -eq $game) {
    foreach ($f in $state.files) { $files[$f] = $true }
    if ($state.backups) { foreach ($p in $state.backups.PSObject.Properties) { $backups[$p.Name] = $p.Value } }
    if ($state.settings_changes) { foreach ($c in $state.settings_changes) { $changes["$($c[0])|$($c[1])"] = @($c[0], $c[1], $c[2], $c[3]) } }
}

# An update keeps the render size (it depends on the headset); the rest of witcher3vr.ini is the tested preset.
$iniPath = Join-Path $x64 'witcher3vr.ini'
$keepWidth = $null; $keepHeight = $null
if (Test-Path -LiteralPath $iniPath) {
    $old = Read-Ini $iniPath
    $keepWidth = Get-IniValue $old 'openxr' 'render_width'
    $keepHeight = Get-IniValue $old 'openxr' 'render_height'
    New-Item -ItemType Directory -Force -Path (Join-Path $StateDir 'ini-backups') | Out-Null
    $saved = Join-Path $StateDir ("ini-backups\witcher3vr.ini." + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    Copy-Item -LiteralPath $iniPath -Destination $saved -Force
    Write-Host "Updating: witcher3vr.ini is replaced by this release's tested settings (render size kept); the old one is saved as $saved"
}

Write-Host 'Copying the mod files ...'
foreach ($top in 'bin', 'mods', 'dlc', 'Witcher3VR') {
    $src = Join-Path $Payload $top
    if (-not (Test-Path -LiteralPath $src)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $src -Recurse -File) {
        $rel = $file.FullName.Substring($Payload.Length).TrimStart('\')
        $dest = Join-Path $game $rel
        if ((Test-Path -LiteralPath $dest) -and -not $files.ContainsKey($rel)) {
            $name = [IO.Path]::GetFileName($dest).ToLowerInvariant()
            $foreign = $false
            if ($name -eq 'dxgi.dll' -or $name -eq 'openxr_loader.dll') {
                if ((Get-FileHash -LiteralPath $dest).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) {
                    $foreign = $true
                    if ($name -eq 'dxgi.dll') {
                        $bytes = [IO.File]::ReadAllBytes($dest)
                        $foreign = ([Text.Encoding]::ASCII.GetString($bytes).IndexOf('witcher3vr') -lt 0)
                    }
                }
            }
            if ($foreign) {
                # Someone else's file (e.g. a ReShade dxgi.dll): keep it for the uninstaller.
                $keep = Join-Path $StateDir ("overwritten\" + $rel)
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $keep) | Out-Null
                Copy-Item -LiteralPath $dest -Destination $keep -Force
                $backups[$rel] = $keep
                Write-Host "  kept your existing $rel for the uninstaller"
            }
        }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $dest -Force
        $files[$rel] = $true
    }
}
if ($keepWidth -and $keepHeight) {
    $ini = Read-Ini $iniPath
    Set-IniValue $ini 'openxr' 'render_width' $keepWidth
    Set-IniValue $ini 'openxr' 'render_height' $keepHeight
    Save-Ini $ini
    Remove-Item -LiteralPath ($iniPath + '.w3vr.bak') -ErrorAction SilentlyContinue
}
Write-Host "  $($files.Count) files in place."

# VR graphics settings, as the tested Linux setup writes them (Remastered 5.01).
$settingsPath = Join-Path $docs 'dx12user.settings'
$settingsDone = $false
if (Test-Path -LiteralPath $settingsPath) {
    $nvidia = $false
    try { $nvidia = [bool](Get-CimInstance Win32_VideoController | Where-Object { $_.Name -match 'NVIDIA' }) } catch { }
    $ini = Read-Ini $iniPath
    $width = Get-IniValue $ini 'openxr' 'render_width'
    $height = Get-IniValue $ini 'openxr' 'render_height'
    $wanted = @(
        @('W3VRSettings', 'CameraFollowPolicy', '1'),
        @('W3VRSettings', 'HideStaticHudOutsideCombat', 'false'),
        @('Gameplay', 'AutoCameraCenter', 'false'),
        @('Gameplay', 'EnableAlternateExplorationCamera', '1'),
        @('Gameplay', 'EnableAlternateCombatCamera', '1'),
        @('Rendering', 'AllowDLSSG', 'false'),
        @('Rendering', 'AllowReflex', 'false'),
        @('Rendering', 'AllowDLSSRR', 'false'),
        @('PostProcess', 'EnableDLSSRR', 'false'),
        @('PostProcess', 'SharpenAmount', '0'),
        @('Rendering/TAA', 'EnableCAS', 'false'),
        @('Hidden', 'uiHorizontalFrameScale', '1'),
        @('Hidden', 'uiVerticalFrameScale', '1')
    )
    if ($width -and $height) { $wanted += ,@('Viewport', 'Resolution', ('"{0}x{1}"' -f $width, $height)) }
    if ($nvidia) {
        $wanted += ,@('PostProcess', 'AAMode', '6')
        $wanted += ,@('PostProcess', 'DLSSQuality', '1')
        $wanted += ,@('Rendering', 'AllowDLSS', 'true')
    } else {
        $wanted += ,@('PostProcess', 'AAMode', '5')  # FSR, one history per eye (remaster_fsr_per_eye=1)
    }
    $doc = Read-Ini $settingsPath
    foreach ($w in $wanted) {
        $before = Get-IniValue $doc $w[0] $w[1]
        $key = "$($w[0])|$($w[1])"
        if ($before -ne $w[2]) {
            if (-not $changes.ContainsKey($key)) { $changes[$key] = @($w[0], $w[1], $before, $w[2]) }
            Set-IniValue $doc $w[0] $w[1] $w[2]
        }
        if ($changes.ContainsKey($key)) { $changes[$key][3] = $w[2] }
    }
    Save-Ini $doc
    Write-Host ('VR graphics settings written ({0}).' -f $(if ($nvidia) { 'DLSS Quality' } else { 'FSR' }))
    $settingsDone = $true
} else {
    Write-Host 'The game has not created its settings yet (first start). Start the game once (main menu is enough),'
    Write-Host 'quit, and run the installer again to apply the VR graphics settings.'
}

New-Item -ItemType Directory -Force -Path $StateDir | Out-Null
$manifest = [ordered]@{
    game = $game
    installed_at = (Get-Date -Format 's')
    files = @($files.Keys | Sort-Object)
    backups = $backups
    settings_changes = @($changes.Values)
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $StateFile -Encoding UTF8

Write-Host ''
Write-Host ('=' * 72)
if ($settingsDone) { Write-Host 'Done.' } else { Write-Host 'Done, with the step above to finish later.' }
Write-Host 'Play: start your VR runtime (SteamVR, Virtual Desktop, Quest Link, ...), connect the headset,'
Write-Host 'then start The Witcher 3 in Steam. Remove the mod again any time with "Uninstall (Windows)".'
Write-Host ('=' * 72)
