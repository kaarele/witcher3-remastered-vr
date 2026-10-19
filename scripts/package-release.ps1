[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[Vv][0-9]+$')]
    [string] $Version,

    [string] $OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist'),

    [string] $PureDarkPlugin,

    [string] $OfxrLayer,

    [string] $OptiScalerDll,

    [string] $OptiScalerIni,

    [string] $OptiScalerLicense,

    [string] $RenderDocDll,

    [string] $RenderDocLicense,

    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string] $ArtifactName
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
$repositoryPrefix = $repositoryRoot.TrimEnd('\') + '\'
if (-not $resolvedOutput.StartsWith(
        $repositoryPrefix,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be inside the repository workspace.'
}
$OutputDirectory = $resolvedOutput
$normalizedVersion = $Version.ToUpperInvariant()
$pureDarkPluginDefault = Join-Path $repositoryRoot `
    'external/puredark-afw/PDAFWPlugin.dll'
if ([string]::IsNullOrWhiteSpace($PureDarkPlugin)) {
    $PureDarkPlugin = $pureDarkPluginDefault
} else {
    $PureDarkPlugin = [System.IO.Path]::GetFullPath($PureDarkPlugin)
}
$ofxrLayerDefault = Join-Path $repositoryRoot `
    'runtime/XR_APILAYER_XRFrameBridge_diagnostic.dll'
if ([string]::IsNullOrWhiteSpace($OfxrLayer)) {
    $OfxrLayer = $ofxrLayerDefault
} else {
    $OfxrLayer = [System.IO.Path]::GetFullPath($OfxrLayer)
}
$optiscalerDllDefault = Join-Path $repositoryRoot `
    'runtime/witcher3vr-optiscaler-dlss5-reference/OptiScaler.dll'
if ([string]::IsNullOrWhiteSpace($OptiScalerDll)) {
    $OptiScalerDll = $optiscalerDllDefault
} else {
    $OptiScalerDll = [System.IO.Path]::GetFullPath($OptiScalerDll)
}
$optiscalerLicenseDefault = Join-Path $repositoryRoot `
    'external/optiscaler/LICENSE'
if ([string]::IsNullOrWhiteSpace($OptiScalerLicense)) {
    $OptiScalerLicense = $optiscalerLicenseDefault
} else {
    $OptiScalerLicense = [System.IO.Path]::GetFullPath($OptiScalerLicense)
}
if ([string]::IsNullOrWhiteSpace($OptiScalerIni)) {
    $OptiScalerIni = Join-Path $repositoryRoot 'support/OptiScaler.release.ini'
}
$renderDocDllDefault = Join-Path $repositoryRoot `
    'external/renderdoc/renderdoc.dll'
if ([string]::IsNullOrWhiteSpace($RenderDocDll)) {
    $RenderDocDll = $renderDocDllDefault
} else {
    $RenderDocDll = [System.IO.Path]::GetFullPath($RenderDocDll)
}
$renderDocLicenseDefault = Join-Path $repositoryRoot `
    'external/renderdoc/LICENSE.md'
if ([string]::IsNullOrWhiteSpace($RenderDocLicense)) {
    $RenderDocLicense = $renderDocLicenseDefault
} else {
    $RenderDocLicense = [System.IO.Path]::GetFullPath($RenderDocLicense)
}
if ([string]::IsNullOrWhiteSpace($ArtifactName)) {
    $ArtifactName = "Witcher3VR-$normalizedVersion"
}
$pureDarkIni = Join-Path $repositoryRoot 'puredark_afw.ini'
$ofxrManifest = Join-Path $repositoryRoot `
    'XR_APILAYER_XRFrameBridge_diagnostic.json'
$ofxrIni = Join-Path $repositoryRoot 'ofxr_bridge.ini'
$optiscalerBridgeIni = Join-Path $repositoryRoot 'optiscaler_bridge.ini'
$optiscalerPayload = Join-Path $repositoryRoot 'OPTISCALER_PAYLOAD.md'
$releaseDll = Join-Path $repositoryRoot 'build/release/Release/dxgi.dll'
$launcher = Join-Path $repositoryRoot 'build/release/launcher/Release/Witcher3VRLauncher.exe'
$openXrLoaderSource = Join-Path $repositoryRoot 'external/openxr-loader'
$openXrLoaderBuild = Join-Path $repositoryRoot 'build/openxr-loader'
$openXrLoader = Join-Path $openXrLoaderBuild `
    'src/loader/Release/openxr_loader.dll'
$exampleIni = Join-Path $repositoryRoot 'witcher3vr.example.ini'
$readme = Join-Path $repositoryRoot 'README.md'
$license = Join-Path $repositoryRoot 'LICENSE'
$thirdPartyNotices = Join-Path $repositoryRoot 'THIRD_PARTY_NOTICES.md'
$releaseNotes = Join-Path $repositoryRoot 'docs/releases/v0.9.8.md'
$componentLicenses = Join-Path $repositoryRoot 'licenses'
$reshadeReference = Join-Path $repositoryRoot 'runtime/witcher3vr-reshade-reference'
$modifiedOptiscalerReference = Join-Path $repositoryRoot 'runtime/witcher3vr-optiscaler-dlss5-reference'
$stateBridgeRoot = Join-Path $repositoryRoot 'support/modWitcher3VRStateBridge'
$stateBridgeScript = Join-Path $stateBridgeRoot `
    'content/scripts/local/witcher3vr/first_person_state_bridge.ws'
$firstPersonRoot = Join-Path $repositoryRoot 'support/modWitcher3VRFirstPerson'
$firstPersonAimScript = Join-Path $firstPersonRoot `
    'content/scripts/local/witcher3vr_first_person/first_person_aim.ws'
$firstPersonHeadScript = Join-Path $firstPersonRoot `
    'content/scripts/local/witcher3vr_first_person/native_head_provider.ws'
$firstPersonStrafeScript = Join-Path $firstPersonRoot `
    'content/scripts/local/witcher3vr_first_person/on_foot_strafe.ws'
$firstPersonVisibilityScript = Join-Path $firstPersonRoot `
    'content/scripts/local/witcher3vr_first_person/player_visibility.ws'
$hudEditorRoot = Join-Path $repositoryRoot 'support/modWitcher3VRHUDEditor'
$hudEditorScript = Join-Path $hudEditorRoot `
    'content/scripts/local/witcher3vr_hud_editor/hud_editor.ws'
$hudEditorXml = Join-Path $repositoryRoot `
    'support/modWitcher3VRHUDEditor.xml'
# Optional, 4.04 only: the Fast Transitions DLC is not part of this repository (its redistribution
# rights are undocumented). Put the original release's dlc/dlcmovementinputfix here to include it.
$movementDlcRoot = Join-Path $repositoryRoot 'support/dlcmovementinputfix'
$movementDlcBundle = Join-Path $movementDlcRoot 'content/blob0.bundle'
$movementDlcMetadata = Join-Path $movementDlcRoot 'content/metadata.store'
$hasMovementDlc = (Test-Path -LiteralPath $movementDlcBundle -PathType Leaf) -and
    (Test-Path -LiteralPath $movementDlcMetadata -PathType Leaf)

if (-not (Test-Path -LiteralPath (Join-Path $openXrLoaderSource 'CMakeLists.txt') `
        -PathType Leaf)) {
    throw 'Missing OpenXR loader source. Run scripts/setup-dependencies.ps1 first.'
}
if (-not (Test-Path -LiteralPath $openXrLoader -PathType Leaf)) {
    & cmake -S $openXrLoaderSource -B $openXrLoaderBuild -A x64 `
        -DDYNAMIC_LOADER=ON
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to configure the pinned OpenXR loader.'
    }
    & cmake --build $openXrLoaderBuild --config Release --target openxr_loader
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to build the pinned OpenXR loader.'
    }
}

foreach ($requiredFile in @(
        $releaseDll,
        $launcher,
        $openXrLoader,
        $PureDarkPlugin,
        $pureDarkIni,
        $OfxrLayer,
        $ofxrManifest,
        $ofxrIni,
        $OptiScalerDll,
        $OptiScalerIni,
        $OptiScalerLicense,
        $RenderDocDll,
        $RenderDocLicense,
        $optiscalerBridgeIni,
        $optiscalerPayload,
        $exampleIni,
        $readme,
        $license,
        $thirdPartyNotices,
        $releaseNotes,
        (Join-Path $componentLicenses 'GPL-3.0.txt'),
        (Join-Path $componentLicenses 'OFXR-LGPL-3.0.txt'),
        (Join-Path $componentLicenses 'AMD-FidelityFX-MIT.txt'),
        (Join-Path $componentLicenses 'NVIDIA-Optical-Flow-Headers.txt'),
        (Join-Path $componentLicenses 'Dear-ImGui-MIT.txt'),
        (Join-Path $reshadeReference 'ReShade64.dll'),
        (Join-Path $modifiedOptiscalerReference 'nvngx.dll_dlssnr.dll'),
        (Join-Path $modifiedOptiscalerReference 'amd_fidelityfx_framegeneration_dx12.dll'),
        (Join-Path $modifiedOptiscalerReference 'ofxr_amd_fidelityfx_framegeneration_dx12.dll'),
        $stateBridgeScript,
        $firstPersonAimScript,
        $firstPersonHeadScript,
        $firstPersonStrafeScript,
        $firstPersonVisibilityScript,
        $hudEditorScript,
        $hudEditorXml)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Missing release input: $requiredFile"
    }
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$stagingRoot = Join-Path $OutputDirectory '.staging'
if (Test-Path -LiteralPath $stagingRoot) {
    throw "Staging directory already exists; use a fresh OutputDirectory: $stagingRoot"
}
New-Item -ItemType Directory -Path $stagingRoot | Out-Null

try {
    $manifestLines = @("Version=$normalizedVersion")
    $commit = git -C $repositoryRoot rev-parse --verify HEAD 2>$null
    if ($LASTEXITCODE -eq 0) {
        $manifestLines += "Commit=$commit"
        $trackedChanges = git -C $repositoryRoot status --porcelain `
            --untracked-files=no
        if ($trackedChanges) {
            $manifestLines += 'Worktree=dirty'
        } else {
            $manifestLines += 'Worktree=clean'
        }
    }

    # The archive mirrors the game root. Users extract it directly into
    # "The Witcher 3" instead of manually relocating the DLL, launcher or mod.
    $releaseStage = Join-Path $stagingRoot 'game-root'
    New-Item -ItemType Directory -Path $releaseStage | Out-Null

    $binaryStage = Join-Path $releaseStage 'bin/x64_dx12'
    New-Item -ItemType Directory -Path $binaryStage | Out-Null
    Copy-Item -LiteralPath $launcher -Destination $binaryStage
    Copy-Item -LiteralPath $openXrLoader -Destination $binaryStage
    Copy-Item -LiteralPath $PureDarkPlugin `
        -Destination (Join-Path $binaryStage 'PDAFWPlugin.dll')
    Copy-Item -LiteralPath $pureDarkIni -Destination $binaryStage
    Copy-Item -LiteralPath $OfxrLayer -Destination `
        (Join-Path $binaryStage 'XR_APILAYER_XRFrameBridge_diagnostic.dll')
    Copy-Item -LiteralPath $ofxrManifest -Destination $binaryStage
    Copy-Item -LiteralPath $ofxrIni -Destination $binaryStage

    # Reference sources only: the launcher creates active root aliases.
    $modReferenceStage = Join-Path $binaryStage 'witcher3vr-mod-reference'
    $modifiedReferenceStage = Join-Path $binaryStage 'witcher3vr-optiscaler-dlss5-reference'
    $reshadeReferenceStage = Join-Path $binaryStage 'witcher3vr-reshade-reference'
    $dlss5ReferenceStage = Join-Path $binaryStage 'witcher3vr-dlss5-reference'
    foreach ($directory in @($modReferenceStage,
            $modifiedReferenceStage, $reshadeReferenceStage,
            $dlss5ReferenceStage)) {
        New-Item -ItemType Directory -Path $directory | Out-Null
    }
    Copy-Item -LiteralPath $releaseDll -Destination (Join-Path $modReferenceStage 'dxgi.dll')
    Copy-Item -LiteralPath $optiscalerBridgeIni -Destination $binaryStage
    Copy-Item -LiteralPath (Join-Path $reshadeReference 'ReShade64.dll') -Destination $reshadeReferenceStage
    Copy-Item -LiteralPath $OptiScalerDll -Destination (Join-Path $modifiedReferenceStage 'OptiScaler.dll')
    Copy-Item -LiteralPath $OptiScalerIni -Destination (Join-Path $binaryStage 'OptiScaler.ini')
    foreach ($name in @('nvngx.dll_dlssnr.dll',
            'amd_fidelityfx_framegeneration_dx12.dll',
            'ofxr_amd_fidelityfx_framegeneration_dx12.dll')) {
        Copy-Item -LiteralPath (Join-Path $modifiedOptiscalerReference $name) -Destination $modifiedReferenceStage
    }
    # Never read or copy a private NVIDIA source: the DLSS5 folder stays empty.

    $modsStage = Join-Path $releaseStage 'mods'
    New-Item -ItemType Directory -Path $modsStage | Out-Null
    Copy-Item -LiteralPath $stateBridgeRoot `
        -Destination (Join-Path $modsStage 'modWitcher3VRStateBridge') -Recurse
    Copy-Item -LiteralPath $firstPersonRoot `
        -Destination (Join-Path $modsStage 'modWitcher3VRFirstPerson') -Recurse
    Copy-Item -LiteralPath $hudEditorRoot `
        -Destination (Join-Path $modsStage 'modWitcher3VRHUDEditor') -Recurse

    $configMatrixStage = Join-Path $releaseStage `
        'bin/config/r4game/user_config_matrix/pc'
    New-Item -ItemType Directory -Path $configMatrixStage -Force | Out-Null
    Copy-Item -LiteralPath $hudEditorXml -Destination $configMatrixStage

    if ($hasMovementDlc) {
        $dlcStage = Join-Path $releaseStage 'dlc'
        New-Item -ItemType Directory -Path $dlcStage | Out-Null
        Copy-Item -LiteralPath $movementDlcRoot `
            -Destination (Join-Path $dlcStage 'dlcmovementinputfix') -Recurse
    }

    $documentationStage = Join-Path $releaseStage 'Witcher3VR'
    $configStage = Join-Path $documentationStage 'config'
    New-Item -ItemType Directory -Path $configStage -Force | Out-Null
    Copy-Item -LiteralPath $exampleIni `
        -Destination (Join-Path $configStage 'witcher3vr.example.ini')
    Copy-Item -LiteralPath $readme -Destination $documentationStage
    Copy-Item -LiteralPath $license -Destination $documentationStage
    Copy-Item -LiteralPath $thirdPartyNotices -Destination $documentationStage
    Copy-Item -LiteralPath $releaseNotes -Destination $documentationStage
    Copy-Item -LiteralPath $componentLicenses -Destination $documentationStage -Recurse
    Set-Content -LiteralPath (Join-Path $documentationStage 'BUILD.txt') `
        -Value @(
            $normalizedVersion,
            'One optimized DLL for gaming and diagnostics.',
            'Use Diagnostic Logging in the launcher when support logs are needed.',
            'Start Witcher3VRLauncher.exe and Save before launching the game.',
            'DLSS5 files are not bundled. Put nvngx_dlssnr.dll from your separately obtained package in bin/x64_dx12/witcher3vr-dlss5-reference.'
        ) -Encoding utf8

    Copy-Item -LiteralPath $optiscalerPayload -Destination $documentationStage
    $optiscalerLicensesStage = Join-Path $documentationStage 'OptiScaler-Licenses'
    New-Item -ItemType Directory -Path $optiscalerLicensesStage | Out-Null
    Copy-Item -LiteralPath $OptiScalerLicense -Destination (Join-Path $optiscalerLicensesStage 'OptiScaler-GPL-3.0.txt')

    $renderDocStage = Join-Path $stagingRoot 'renderdoc-addon'
    $renderDocBinaryStage = Join-Path $renderDocStage 'bin/x64_dx12'
    $renderDocDocumentationStage = Join-Path $renderDocStage 'Witcher3VR'
    New-Item -ItemType Directory -Path $renderDocBinaryStage -Force | Out-Null
    New-Item -ItemType Directory -Path $renderDocDocumentationStage `
        -Force | Out-Null
    Copy-Item -LiteralPath $RenderDocDll -Destination `
        (Join-Path $renderDocBinaryStage 'renderdoc.dll')
    Copy-Item -LiteralPath $RenderDocLicense -Destination `
        (Join-Path $renderDocDocumentationStage 'RENDERDOC-LICENSE.md')
    Set-Content -LiteralPath `
        (Join-Path $renderDocDocumentationStage `
            'README-RENDERDOC-ADDON.txt') -Value @(
            "RenderDoc Addon for $ArtifactName",
            '',
            'This optional diagnostic addon is not required to play.',
            'Install it only when RenderDoc captures are needed.',
            'Extract this archive into the Witcher 3 game folder.'
        ) -Encoding utf8

    # Fail closed if any NVIDIA DLSS runtime was accidentally added elsewhere.
    $nvidiaNames = @('nvngx_dlss.dll', 'nvngx_dlssg.dll', 'nvngx_dlssnr.dll')
    $forbidden = @(Get-ChildItem -LiteralPath $releaseStage -File -Recurse |
        Where-Object { $_.Name -in $nvidiaNames })
    if ($forbidden.Count -ne 0 -or
        @(Get-ChildItem -LiteralPath $dlss5ReferenceStage -Force).Count -ne 0) {
        throw 'NVIDIA DLSS5 runtimes must not be distributed; its reference must be empty.'
    }

    $archivePath = Join-Path $OutputDirectory "$ArtifactName.zip"
    if (Test-Path -LiteralPath $archivePath) {
        throw "Archive already exists; use a fresh OutputDirectory: $archivePath"
    }
    Compress-Archive -Path (Join-Path $releaseStage '*') -DestinationPath $archivePath
    $renderDocArchivePath = Join-Path $OutputDirectory `
        "$ArtifactName-RenderDoc-Addon.zip"
    if (Test-Path -LiteralPath $renderDocArchivePath) {
        throw "Archive already exists; use a fresh OutputDirectory: $renderDocArchivePath"
    }
    Compress-Archive -Path (Join-Path $renderDocStage '*') `
        -DestinationPath $renderDocArchivePath

    $dllHash = (Get-FileHash -LiteralPath $releaseDll -Algorithm SHA256).Hash
    $launcherHash = (Get-FileHash -LiteralPath $launcher -Algorithm SHA256).Hash
    $openXrLoaderHash =
        (Get-FileHash -LiteralPath $openXrLoader -Algorithm SHA256).Hash
    $pureDarkPluginHash =
        (Get-FileHash -LiteralPath $PureDarkPlugin -Algorithm SHA256).Hash
    $pureDarkIniHash =
        (Get-FileHash -LiteralPath $pureDarkIni -Algorithm SHA256).Hash
    $ofxrLayerHash =
        (Get-FileHash -LiteralPath $OfxrLayer -Algorithm SHA256).Hash
    $ofxrManifestHash =
        (Get-FileHash -LiteralPath $ofxrManifest -Algorithm SHA256).Hash
    $ofxrIniHash =
        (Get-FileHash -LiteralPath $ofxrIni -Algorithm SHA256).Hash
    $optiscalerDllHash =
        (Get-FileHash -LiteralPath $OptiScalerDll -Algorithm SHA256).Hash
    $optiscalerBridgeIniHash =
        (Get-FileHash -LiteralPath $optiscalerBridgeIni -Algorithm SHA256).Hash
    $renderDocDllHash =
        (Get-FileHash -LiteralPath $RenderDocDll -Algorithm SHA256).Hash
    $hudEditorScriptHash =
        (Get-FileHash -LiteralPath $hudEditorScript -Algorithm SHA256).Hash
    $hudEditorXmlHash =
        (Get-FileHash -LiteralPath $hudEditorXml -Algorithm SHA256).Hash
    $firstPersonAimHash =
        (Get-FileHash -LiteralPath $firstPersonAimScript -Algorithm SHA256).Hash
    $firstPersonHeadHash =
        (Get-FileHash -LiteralPath $firstPersonHeadScript -Algorithm SHA256).Hash
    $firstPersonStrafeHash =
        (Get-FileHash -LiteralPath $firstPersonStrafeScript -Algorithm SHA256).Hash
    $firstPersonVisibilityHash =
        (Get-FileHash -LiteralPath $firstPersonVisibilityScript -Algorithm SHA256).Hash
    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    $renderDocArchiveHash =
        (Get-FileHash -LiteralPath $renderDocArchivePath -Algorithm SHA256).Hash
    $manifestLines += "dll.sha256=$dllHash"
    $manifestLines += "launcher.sha256=$launcherHash"
    $manifestLines += "openxr_loader.sha256=$openXrLoaderHash"
    $manifestLines += "puredark_plugin.sha256=$pureDarkPluginHash"
    $manifestLines += "puredark_ini.sha256=$pureDarkIniHash"
    $manifestLines += "ofxr_layer.sha256=$ofxrLayerHash"
    $manifestLines += "ofxr_manifest.sha256=$ofxrManifestHash"
    $manifestLines += "ofxr_ini.sha256=$ofxrIniHash"
    $manifestLines += "optiscaler.sha256=$optiscalerDllHash"
    $manifestLines += "optiscaler_bridge_ini.sha256=$optiscalerBridgeIniHash"
    $manifestLines += "renderdoc_custom_v1273.sha256=$renderDocDllHash"
    if ($hasMovementDlc) {
        $manifestLines += "movement_dlc_bundle.sha256=$((Get-FileHash -LiteralPath $movementDlcBundle -Algorithm SHA256).Hash)"
        $manifestLines += "movement_dlc_metadata.sha256=$((Get-FileHash -LiteralPath $movementDlcMetadata -Algorithm SHA256).Hash)"
    }
    $manifestLines += "hud_editor_script.sha256=$hudEditorScriptHash"
    $manifestLines += "hud_editor_xml.sha256=$hudEditorXmlHash"
    $manifestLines += "first_person_aim.sha256=$firstPersonAimHash"
    $manifestLines += "first_person_head.sha256=$firstPersonHeadHash"
    $manifestLines += "first_person_strafe.sha256=$firstPersonStrafeHash"
    $manifestLines += "first_person_visibility.sha256=$firstPersonVisibilityHash"
    $manifestLines += "main_archive.sha256=$archiveHash"
    $manifestLines += "renderdoc_addon_archive.sha256=$renderDocArchiveHash"

    $manifestPath = Join-Path $OutputDirectory "$ArtifactName-SHA256.txt"
    Set-Content -LiteralPath $manifestPath -Value $manifestLines -Encoding utf8
    Write-Host "Release packages written to $OutputDirectory"
} finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        $resolvedStaging = (Resolve-Path -LiteralPath $stagingRoot).Path
        if (-not $resolvedStaging.StartsWith($OutputDirectory.TrimEnd('\') + '\',
                [System.StringComparison]::OrdinalIgnoreCase) -or
            (Get-Item -LiteralPath $stagingRoot -Force).Attributes -band
                [System.IO.FileAttributes]::ReparsePoint) {
            throw 'Refusing staging cleanup outside the package output directory.'
        }
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
}
