<#
.SYNOPSIS
    Remove the VideoPlayer default-app registration written by
    register-default-app.ps1 (per-user / HKCU only).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File unregister-default-app.ps1
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$ProgId     = 'CustomMedia.VideoPlayer'
$AppRegName = 'VideoPlayer'

$Extensions = @(
    '.mp4', '.mkv', '.avi', '.mov', '.wmv', '.flv', '.webm',
    '.m4v', '.ts',  '.m2ts','.mpg', '.mpeg','.3gp', '.ogv'
)

# 1) RegisteredApplications entry
$regAppsKey = 'HKCU:\Software\RegisteredApplications'
if (Test-Path $regAppsKey) {
    Remove-ItemProperty -Path $regAppsKey -Name $AppRegName -ErrorAction SilentlyContinue
}

# 2) Capabilities tree
Remove-Item -Path "HKCU:\Software\$AppRegName" -Recurse -Force -ErrorAction SilentlyContinue

# 3) ProgID
Remove-Item -Path "HKCU:\Software\Classes\$ProgId" -Recurse -Force -ErrorAction SilentlyContinue

# 4) OpenWithProgids back-references
foreach ($ext in $Extensions) {
    $owp = "HKCU:\Software\Classes\$ext\OpenWithProgids"
    if (Test-Path $owp) {
        Remove-ItemProperty -Path $owp -Name $ProgId -ErrorAction SilentlyContinue
    }
}

Write-Host "Unregistered '$AppRegName'." -ForegroundColor Green
Write-Host "Any default-app assignment you made in Settings may still need" -ForegroundColor Yellow
Write-Host "to be reassigned to another app manually."
