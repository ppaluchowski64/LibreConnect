Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$dllPath = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot "..\..\..\virtual-camera-platform-implementation.dll")
)

if (-not (Test-Path $dllPath -PathType Leaf)) {
    throw "virtual-camera-platform-implementation.dll not found: $dllPath"
}

$regsvr32Exe = Join-Path $env:WINDIR "System32\regsvr32.exe"
if (-not (Test-Path $regsvr32Exe -PathType Leaf)) {
    throw "regsvr32.exe not found: $regsvr32Exe"
}

$process = Start-Process `
    -FilePath $regsvr32Exe `
    -ArgumentList @("/s", "`"$dllPath`"") `
    -WindowStyle Hidden `
    -Wait `
    -PassThru

if ($process.ExitCode -ne 0) {
    throw "regsvr32 failed with exit code $($process.ExitCode)."
}
