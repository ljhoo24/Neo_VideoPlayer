<#
.SYNOPSIS
    Register VideoPlayer with Windows as a selectable default media app.

.DESCRIPTION
    Writes a per-user (HKCU) ProgID, app Capabilities, and file
    associations so VideoPlayer shows up in:
      - Right-click > "Open with" > Choose another app
      - Settings > Apps > Default apps

    No administrator rights required (everything lives under HKCU).

    Windows 10/11 does NOT let an app force itself as the default — after
    running this, open "Settings > Default apps", find VideoPlayer, and
    assign the file types you want. This script only makes the app a valid
    *candidate* and wires up the launch command.

.PARAMETER ExePath
    Full path to VideoPlayer.exe. If omitted, the script searches common
    build output folders next to the repo root.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File register-default-app.ps1
    powershell -ExecutionPolicy Bypass -File register-default-app.ps1 -ExePath "C:\path\to\VideoPlayer.exe"
#>

[CmdletBinding()]
param(
    [string]$ExePath
)

$ErrorActionPreference = 'Stop'

# ---- ProgID + app identity (keep in sync with main.cpp set*Name calls) ----
$ProgId      = 'CustomMedia.VideoPlayer'
$AppRegName  = 'VideoPlayer'
$AppFriendly = 'VideoPlayer'
$AppDesc     = 'Custom Qt6 + libmpv media player'

# Extensions must match videoExtensions() in MainWindow.cpp
$Extensions = @(
    '.mp4', '.mkv', '.avi', '.mov', '.wmv', '.flv', '.webm',
    '.m4v', '.ts',  '.m2ts','.mpg', '.mpeg','.3gp', '.ogv'
)

# ------------------------------------------------------------
# Locate the executable
# ------------------------------------------------------------
if (-not $ExePath) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $candidates = Get-ChildItem -Path $repoRoot -Recurse -Filter 'VideoPlayer.exe' -ErrorAction SilentlyContinue |
                  Sort-Object LastWriteTime -Descending
    if ($candidates) { $ExePath = $candidates[0].FullName }
}

if (-not $ExePath -or -not (Test-Path $ExePath)) {
    Write-Error "VideoPlayer.exe not found. Build first, or pass -ExePath <path>."
    return
}

$ExePath = (Resolve-Path $ExePath).Path
Write-Host "Using executable: $ExePath" -ForegroundColor Cyan

# ------------------------------------------------------------
# 1) ProgID — how Windows launches the file
# ------------------------------------------------------------
$progKey = "HKCU:\Software\Classes\$ProgId"
New-Item -Path "$progKey\shell\open\command" -Force | Out-Null
New-Item -Path "$progKey\DefaultIcon"        -Force | Out-Null
Set-ItemProperty -Path $progKey -Name '(Default)' -Value 'VideoPlayer Media File'
Set-ItemProperty -Path "$progKey\DefaultIcon"      -Name '(Default)' -Value "`"$ExePath`",0"
Set-ItemProperty -Path "$progKey\shell\open\command" -Name '(Default)' -Value "`"$ExePath`" `"%1`""

# ------------------------------------------------------------
# 2) Capabilities + FileAssociations — feeds the "Default apps" UI
# ------------------------------------------------------------
$capKey = "HKCU:\Software\$AppRegName\Capabilities"
New-Item -Path "$capKey\FileAssociations" -Force | Out-Null
Set-ItemProperty -Path $capKey -Name 'ApplicationName'        -Value $AppFriendly
Set-ItemProperty -Path $capKey -Name 'ApplicationDescription' -Value $AppDesc

foreach ($ext in $Extensions) {
    Set-ItemProperty -Path "$capKey\FileAssociations" -Name $ext -Value $ProgId
    # Make sure the extension knows about our ProgID as an option too.
    New-Item -Path "HKCU:\Software\Classes\$ext\OpenWithProgids" -Force | Out-Null
    Set-ItemProperty -Path "HKCU:\Software\Classes\$ext\OpenWithProgids" -Name $ProgId -Value ([byte[]]@()) -Type Binary
}

# ------------------------------------------------------------
# 3) RegisteredApplications — makes the app appear in Default apps
# ------------------------------------------------------------
$regAppsKey = 'HKCU:\Software\RegisteredApplications'
New-Item -Path $regAppsKey -Force | Out-Null
Set-ItemProperty -Path $regAppsKey -Name $AppRegName -Value "Software\$AppRegName\Capabilities"

Write-Host ""
Write-Host "Registered '$AppFriendly' as a default-app candidate." -ForegroundColor Green
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Open  Settings > Apps > Default apps"
Write-Host "  2. Search 'VideoPlayer' (or pick a file type like .mp4)"
Write-Host "  3. Assign the extensions you want."
Write-Host ""
Write-Host "Or just right-click a video > Open with > Choose another app > VideoPlayer > Always."
