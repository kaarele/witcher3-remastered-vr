# Witcher 3 Remastered VR - Windows uninstaller, run by "Uninstall (Windows).bat".
# Undoes what install.ps1 did: removes the mod files, puts back files it replaced and restores the VR graphics
# settings the installer changed (unless you changed them again since). Saves are never touched.
. (Join-Path $PSScriptRoot 'common.ps1')

Write-Host 'Witcher 3 Remastered VR - uninstaller'
Write-Host '-------------------------------------'
$state = Read-State
if (-not $state) {
    Write-Host 'Nothing to remove: no installation by "Install (Windows)" was found.'
    exit 0
}
$game = $state.game
if (Test-GameRunning) { Stop-WithError 'The Witcher 3 is running. Quit the game, then run the uninstaller again.' }

$settingsPath = Join-Path (Get-DocsFolder) 'dx12user.settings'
if ($state.settings_changes -and (Test-Path -LiteralPath $settingsPath)) {
    $doc = Read-Ini $settingsPath
    $restored = 0; $kept = 0
    foreach ($c in $state.settings_changes) {
        if ((Get-IniValue $doc $c[0] $c[1]) -ne $c[3]) { $kept++; continue }  # changed again since: keep it
        Set-IniValue $doc $c[0] $c[1] $c[2]
        $restored++
    }
    Save-Ini $doc
    Write-Host ("dx12user.settings: $restored VR settings restored" + $(if ($kept) { ", $kept you changed since were kept" } else { '' }) + '.')
}

$removed = 0
foreach ($rel in $state.files) {
    $path = Join-Path $game $rel
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force; $removed++ }
}
if ($state.backups) {
    foreach ($p in $state.backups.PSObject.Properties) {
        if (Test-Path -LiteralPath $p.Value) {
            Copy-Item -LiteralPath $p.Value -Destination (Join-Path $game $p.Name) -Force
            Write-Host "  put back your earlier $($p.Name)"
        }
    }
}
# Remove the folders the mod created when they are empty now (deepest first).
$dirs = @($state.files | ForEach-Object { Split-Path -Parent (Join-Path $game $_) } | Sort-Object -Unique |
    Sort-Object { $_.Length } -Descending)
foreach ($d in $dirs) {
    $cur = $d
    while ($cur -and $cur.Length -gt $game.Length -and (Test-Path -LiteralPath $cur) -and
           -not (Get-ChildItem -LiteralPath $cur -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $cur -Force
        $cur = Split-Path -Parent $cur
    }
}
Remove-Item -LiteralPath $StateFile -Force
Write-Host "Removed $removed mod files. The game runs without VR again. Your saves were not touched."
