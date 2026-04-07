param(
    [string]$DeployDir = "..\..\build\desktop\build\Release\deploy\Release\appLibreConnect_desktop",
    [string]$Version = "1.0.0",
    [string]$OutputDir = "..\..\out"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-Tool {
    param(
        [string]$Name,
        [string[]]$CandidatePaths = @()
    )

    $fromPath = Get-Command $Name -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    foreach ($candidate in $CandidatePaths) {
        if (Test-Path $candidate -PathType Leaf) {
            return $candidate
        }
    }

    throw "Required tool '$Name' was not found in PATH or known install paths."
}

function Invoke-External {
    param(
        [string]$Exe,
        [string[]]$Arguments,
        [string]$Description
    )

    Write-Host $Description
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed (exit code $LASTEXITCODE)."
    }
}

function Resolve-FullPath {
    param(
        [string]$BaseDir,
        [string]$PathValue
    )

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $BaseDir $PathValue))
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$deployFull = Resolve-FullPath -BaseDir $scriptDir -PathValue $DeployDir
$outputFull = Resolve-FullPath -BaseDir $scriptDir -PathValue $OutputDir
$installerScriptSourceDir = Resolve-FullPath -BaseDir $scriptDir -PathValue "..\..\scripts\windows\install"

if (-not (Test-Path $deployFull -PathType Container)) {
    throw "Deploy directory does not exist: $deployFull"
}

$mainExe = Join-Path $deployFull "appLibreConnect_desktop.exe"
if (-not (Test-Path $mainExe -PathType Leaf)) {
    throw "Main executable not found: $mainExe"
}

if (-not (Test-Path $installerScriptSourceDir -PathType Container)) {
    throw "Installer script source directory does not exist: $installerScriptSourceDir"
}

$virtualCameraSetupScript = Join-Path $installerScriptSourceDir "virtual-camera-dll-setup.ps1"
if (-not (Test-Path $virtualCameraSetupScript -PathType Leaf)) {
    throw "Post-install script not found: $virtualCameraSetupScript"
}

New-Item -ItemType Directory -Force -Path $outputFull | Out-Null

$wixPathCandidates = @(
    "C:\Program Files\WiX Toolset v6.0\bin\wix.exe",
    "C:\Program Files\WiX Toolset v5.0\bin\wix.exe",
    "C:\Program Files (x86)\WiX Toolset v6.0\bin\wix.exe",
    "C:\Program Files (x86)\WiX Toolset v5.0\bin\wix.exe"
)

$wixExe = Resolve-Tool -Name "wix" -CandidatePaths $wixPathCandidates

$wixVersionOutput = & $wixExe --version 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "Unable to determine WiX version from '$wixExe'."
}
$wixVersionText = $wixVersionOutput | Out-String
$wixMajorMatch = [regex]::Match($wixVersionText, "\b(\d+)\.\d+\.\d+\b")
if (-not $wixMajorMatch.Success) {
    throw "Could not parse WiX version from output: $wixVersionText"
}
$wixMajor = [int]$wixMajorMatch.Groups[1].Value
if ($wixMajor -lt 4) {
    throw "WiX v4+ is required for this script. Found: $wixVersionText"
}

$installerWxs = Join-Path $scriptDir "installer.wxs"
$msiPath = Join-Path $outputFull "LibreConnect-$Version-x64.msi"
$intermediateDir = Join-Path $outputFull "obj"
New-Item -ItemType Directory -Force -Path $intermediateDir | Out-Null

Invoke-External -Exe $wixExe -Description "Building MSI with WiX v6" -Arguments @(
    "build",
    "-arch", "x64",
    "-d", "DeployDir=$deployFull",
    "-d", "InstallerScriptSourceDir=$installerScriptSourceDir",
    "-d", "ProductVersion=$Version",
    "-intermediatefolder", $intermediateDir,
    "-out", $msiPath,
    $installerWxs
)

Write-Host "MSI created at: $msiPath"
