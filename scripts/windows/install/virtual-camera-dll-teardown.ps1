Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-WindowsBuildNumber {
    $currentVersion = Get-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion" -ErrorAction SilentlyContinue
    if ($null -eq $currentVersion -or -not $currentVersion.CurrentBuildNumber) {
        return 0
    }

    $buildNumber = 0
    if ([int]::TryParse([string]$currentVersion.CurrentBuildNumber, [ref]$buildNumber)) {
        return $buildNumber
    }

    return 0
}

if ((Get-WindowsBuildNumber) -lt 22000) {
    Write-Host "Skipping virtual camera unregistration: Windows 11 or newer is required."
    exit 0
}

if (Get-Service -Name "FrameServer" -ErrorAction SilentlyContinue) {
    Stop-Service -Name "FrameServer" -Force -ErrorAction SilentlyContinue
}

$dllPath = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot "..\..\..\virtual-camera-platform-implementation.dll")
)

if (-not (Test-Path $dllPath -PathType Leaf)) {
    # DLL already removed; nothing to unregister.
    exit 0
}

$regsvr32Exe = Join-Path $env:WINDIR "System32\regsvr32.exe"
if (-not (Test-Path $regsvr32Exe -PathType Leaf)) {
    throw "regsvr32.exe not found: $regsvr32Exe"
}

$process = Start-Process `
    -FilePath $regsvr32Exe `
    -ArgumentList @("/u", "/s", "`"$dllPath`"") `
    -WindowStyle Hidden `
    -Wait `
    -PassThru

if ($process.ExitCode -ne 0) {
    throw "regsvr32 /u failed with exit code $($process.ExitCode)."
}
