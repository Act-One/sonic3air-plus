param(
    [string]$Ip,
    [string]$Configuration = "ReleaseUWP",
    [string]$PackagePath,
    [string[]]$DependencyPaths,
    [string]$RomSourcePath,
    [string]$RomTargetFileName = "Sonic_Knuckles_wSonic3.bin",
    [switch]$SkipSign,
    [switch]$SkipRomUpload,
    [switch]$ResetAppData,
    [int]$DiscoveryTimeoutSeconds = 5
)

$ErrorActionPreference = "Stop"

$projectDir = $PSScriptRoot
$packageIdentityPrefix = "siahisaforker.Sonic3AIR_"
$devicePortalPort = 11443
$developmentFilesSubdir = "Scarlet3AIR"

function Get-WinAppDeployPath {
    $candidates = @(
        "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\WinAppDeployCmd.exe",
        "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\WinAppDeployCmd.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\Shared\NuGetPackages\microsoft.windows.sdk.buildtools\10.0.26100.1742\bin\10.0.26100.0\x64\WinAppDeployCmd.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $tool = Get-ChildItem "C:\Program Files (x86)" -Recurse -Filter "WinAppDeployCmd.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\x64\WinAppDeployCmd.exe" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $tool) {
        throw "WinAppDeployCmd.exe was not found."
    }
    return $tool.FullName
}

function Get-PackageRootForConfiguration([string]$targetConfiguration) {
    $packageRoot = Join-Path $projectDir "AppPackages\sonic3air"
    $packageDir = Get-ChildItem $packageRoot -Directory |
        Where-Object { $_.Name -like "*_x64_${targetConfiguration}_Test" } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $packageDir) {
        throw "Could not find an AppPackages output directory for configuration '$targetConfiguration'."
    }
    return $packageDir.FullName
}

function Resolve-PackagePath([string]$targetConfiguration, [string]$explicitPackagePath) {
    if ($explicitPackagePath) {
        $resolved = Resolve-Path $explicitPackagePath -ErrorAction Stop
        return $resolved.Path
    }

    $packageRoot = Get-PackageRootForConfiguration $targetConfiguration
    $msix = Get-ChildItem $packageRoot -Filter "*.msix" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $msix) {
        throw "Could not find an .msix file under '$packageRoot'."
    }
    return $msix.FullName
}

function Resolve-DependencyPaths([string]$resolvedPackagePath, [string[]]$explicitDependencyPaths) {
    if ($explicitDependencyPaths -and $explicitDependencyPaths.Count -gt 0) {
        return @($explicitDependencyPaths | ForEach-Object { (Resolve-Path $_ -ErrorAction Stop).Path })
    }

    $packageDir = Split-Path -Parent $resolvedPackagePath
    $depDir = Join-Path $packageDir "Dependencies\x64"
    if (-not (Test-Path $depDir)) {
        return @()
    }

    return @(Get-ChildItem $depDir -Filter "*.appx" |
        Sort-Object Name |
        Select-Object -ExpandProperty FullName)
}

function Get-XboxIp([string]$explicitIp, [string]$winAppDeployPath, [int]$timeoutSeconds) {
    if ($explicitIp) {
        return $explicitIp
    }

    $output = & $winAppDeployPath devices $timeoutSeconds 2>&1
    $deviceLines = @(
        $output |
        Where-Object { $_ -match '^\d{1,3}(?:\.\d{1,3}){3}\s+[0-9a-fA-F-]{36}\s+.+$' }
    )

    $xboxMatches = @()
    foreach ($line in $deviceLines) {
        if ($line -match '^(?<ip>\d{1,3}(?:\.\d{1,3}){3})\s+(?<guid>[0-9a-fA-F-]{36})\s+(?<name>.+)$') {
            $entry = [PSCustomObject]@{
                Ip = $matches.ip
                Guid = $matches.guid
                Name = $matches.name.Trim()
            }
            if ($entry.Name -match 'XBOX') {
                $xboxMatches += $entry
            }
        }
    }

    if ($xboxMatches.Count -eq 1) {
        return $xboxMatches[0].Ip
    }
    if ($xboxMatches.Count -gt 1) {
        $choices = ($xboxMatches | ForEach-Object { "$($_.Ip) [$($_.Name)]" }) -join ", "
        throw "Multiple Xbox devices were discovered. Re-run with -Ip. Found: $choices"
    }

    throw "No Xbox device was auto-discovered. Re-run with -Ip."
}

function Get-InstalledPackageFullName([string]$winAppDeployPath, [string]$ipAddress) {
    $output = & $winAppDeployPath list -ip $ipAddress 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to query installed packages on Xbox $ipAddress.`n$output"
    }

    $match = $output | Where-Object { $_ -like "${packageIdentityPrefix}*" } | Select-Object -First 1
    return $match
}

function Invoke-WinAppDeploy([string]$winAppDeployPath, [string[]]$arguments, [string]$failureMessage) {
    $output = & $winAppDeployPath @arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        $joinedArgs = $arguments -join " "
        throw "$failureMessage`nCommand: $winAppDeployPath $joinedArgs`n$output"
    }
    return $output
}

function Get-DevicePortalBaseUrl([string]$ipAddress) {
    return "https://${ipAddress}:${devicePortalPort}"
}

function Invoke-Curl([string[]]$arguments, [string]$failureMessage) {
    $output = & curl.exe @arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "$failureMessage`n$output"
    }
    return ($output -join [Environment]::NewLine)
}

function New-DevicePortalContext([string]$baseUrl) {
    $cookieJar = Join-Path $env:TEMP ("s3air_xbox_cookies_" + [guid]::NewGuid().ToString("N") + ".txt")
    Invoke-Curl -arguments @("-k", "-s", "-S", "-c", $cookieJar, ($baseUrl + "/")) -failureMessage "Failed to initialize the Xbox Device Portal session." | Out-Null

    $csrfToken = $null
    foreach ($line in Get-Content $cookieJar -ErrorAction Stop) {
        if ($line.StartsWith("#") -or [string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $parts = $line -split "`t"
        if ($parts.Length -ge 7 -and $parts[5] -eq "CSRF-Token") {
            $csrfToken = $parts[6]
            break
        }
    }

    if (-not $csrfToken) {
        throw "Device Portal did not provide a CSRF token."
    }

    return [PSCustomObject]@{
        BaseUrl = $baseUrl
        CookieJar = $cookieJar
        CsrfToken = $csrfToken
    }
}

function Invoke-DevicePortalGetJson($context, [string]$relativeUri) {
    $response = Invoke-Curl -arguments @("-k", "-s", "-S", "-b", $context.CookieJar, ($context.BaseUrl + $relativeUri)) -failureMessage "Device Portal GET failed for '$relativeUri'."
    return $response | ConvertFrom-Json
}

function Invoke-DevicePortalPost($context, [string]$relativeUri) {
    Invoke-Curl -arguments @(
        "-k",
        "-s",
        "-S",
        "-X", "POST",
        "-b", $context.CookieJar,
        "-H", ("X-CSRF-Token: " + $context.CsrfToken),
        ($context.BaseUrl + $relativeUri)
    ) -failureMessage "Device Portal POST failed for '$relativeUri'." | Out-Null
}

function Get-RomSourcePath([string]$explicitRomSourcePath, [string]$romFilename) {
    if ($explicitRomSourcePath) {
        $resolved = Resolve-Path $explicitRomSourcePath -ErrorAction Stop
        return $resolved.Path
    }

    $defaultDownloadsPath = Join-Path $env:USERPROFILE ("Downloads\" + $romFilename)
    if (Test-Path $defaultDownloadsPath) {
        return (Resolve-Path $defaultDownloadsPath).Path
    }

    return $null
}

function Ensure-DevelopmentFilesFolder($context, [string]$folderName) {
    $rootListing = Invoke-DevicePortalGetJson -context $context -relativeUri "/api/filesystem/apps/files?knownfolderid=DevelopmentFiles"
    $existingFolder = @($rootListing.Items | Where-Object { $_.Type -eq 16 -and $_.Name -eq $folderName } | Select-Object -First 1)
    if ($existingFolder.Count -gt 0) {
        return
    }

    $encodedFolder = [Uri]::EscapeDataString($folderName)
    Invoke-DevicePortalPost -context $context -relativeUri "/api/filesystem/apps/folder?knownfolderid=DevelopmentFiles&newfoldername=${encodedFolder}"
}

function Upload-DevelopmentFile($context, [string]$sourcePath, [string]$targetSubdir) {
    $filename = Split-Path -Leaf $sourcePath
    $encodedTargetSubdir = [Uri]::EscapeDataString($targetSubdir)
    $url = $context.BaseUrl + "/api/filesystem/apps/file?knownfolderid=DevelopmentFiles&path=${encodedTargetSubdir}&extract=false"
    $formField = $filename + "=@" + $sourcePath

    Invoke-Curl -arguments @(
        "--fail-with-body",
        "-k",
        "-s",
        "-S",
        "-b", $context.CookieJar,
        "-H", ("X-CSRF-Token: " + $context.CsrfToken),
        "-F", $formField,
        $url
    ) -failureMessage "Failed to upload '$filename' to Xbox DevelopmentFiles." | Out-Null
}

$resolvedPackagePath = Resolve-PackagePath -targetConfiguration $Configuration -explicitPackagePath $PackagePath
$resolvedDependencyPaths = Resolve-DependencyPaths -resolvedPackagePath $resolvedPackagePath -explicitDependencyPaths $DependencyPaths
$winAppDeployPath = Get-WinAppDeployPath
$ipAddress = Get-XboxIp -explicitIp $Ip -winAppDeployPath $winAppDeployPath -timeoutSeconds $DiscoveryTimeoutSeconds
$devicePortalBaseUrl = Get-DevicePortalBaseUrl -ipAddress $ipAddress
$resolvedRomSourcePath = $null
if (-not $SkipRomUpload) {
    $resolvedRomSourcePath = Get-RomSourcePath -explicitRomSourcePath $RomSourcePath -romFilename $RomTargetFileName
}

Write-Host ("Xbox IP: " + $ipAddress)
Write-Host ("Package: " + $resolvedPackagePath)
if ($resolvedDependencyPaths.Count -gt 0) {
    Write-Host "Dependencies:"
    foreach ($dependencyPath in $resolvedDependencyPaths) {
        Write-Host ("  " + $dependencyPath)
    }
}
if ($resolvedRomSourcePath) {
    Write-Host ("ROM upload source: " + $resolvedRomSourcePath)
}

if (-not $SkipSign) {
    $signScriptPath = Join-Path $projectDir "sign_xbox_package.ps1"
    Write-Host "Signing package..."
    & $signScriptPath -Configuration $Configuration -PackagePath $resolvedPackagePath
    if ($LASTEXITCODE -ne 0) {
        throw "Package signing failed."
    }
}

$installedPackageFullName = Get-InstalledPackageFullName -winAppDeployPath $winAppDeployPath -ipAddress $ipAddress
if ($installedPackageFullName) {
    $uninstallArgs = @("uninstall", "-package", $installedPackageFullName, "-ip", $ipAddress)
    if (-not $ResetAppData) {
        $uninstallArgs += "-preserveAppData"
    }

    Write-Host ("Removing existing package: " + $installedPackageFullName)
    if (-not $ResetAppData) {
        Write-Host "Preserving app data during uninstall."
    }
    Invoke-WinAppDeploy -winAppDeployPath $winAppDeployPath -arguments $uninstallArgs -failureMessage "Failed to remove the existing Xbox package."
}

$installArgs = @("install", "-file", $resolvedPackagePath, "-ip", $ipAddress)
if ($resolvedDependencyPaths.Count -gt 0) {
    $installArgs += "-dependency"
    $installArgs += $resolvedDependencyPaths
}

Write-Host "Installing package on Xbox..."
Invoke-WinAppDeploy -winAppDeployPath $winAppDeployPath -arguments $installArgs -failureMessage "Failed to deploy the Xbox package."

if (-not $SkipRomUpload) {
    if ($resolvedRomSourcePath) {
        Write-Host "Uploading ROM to Xbox DevelopmentFiles staging..."
        $devicePortalContext = New-DevicePortalContext -baseUrl $devicePortalBaseUrl
        Ensure-DevelopmentFilesFolder -context $devicePortalContext -folderName $developmentFilesSubdir
        Upload-DevelopmentFile -context $devicePortalContext -sourcePath $resolvedRomSourcePath -targetSubdir $developmentFilesSubdir
    } else {
        Write-Host ("ROM upload skipped: no source file found for '" + $RomTargetFileName + "'.")
    }
}

Write-Host ""
Write-Host "Xbox deployment complete."
Write-Host ("  Device: " + $ipAddress)
Write-Host ("  Package: " + (Split-Path -Leaf $resolvedPackagePath))
if ($resolvedRomSourcePath) {
    Write-Host ("  ROM staged at: DevelopmentFiles/" + $developmentFilesSubdir + "/" + $RomTargetFileName)
}
