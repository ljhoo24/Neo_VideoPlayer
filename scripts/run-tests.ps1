<#
.SYNOPSIS
    Configure, build, and run the VideoPlayer automated tests on Windows.

.DESCRIPTION
    Uses CMake from PATH when available. Otherwise it locates the CMake and
    MSVC environment bundled with Visual Studio, so a normal PowerShell
    window works without manually opening a Developer Command Prompt.

.PARAMETER Configuration
    Release (default) or Debug.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\run-tests.ps1
#>

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$presetSuffix = $Configuration.ToLowerInvariant()
$configurePreset = "windows-x64-$presetSuffix"
$buildPreset = $presetSuffix

function Find-VisualStudioTool([string]$relativePath) {
    $vsRoot = Join-Path $env:ProgramFiles 'Microsoft Visual Studio'
    if (-not (Test-Path -LiteralPath $vsRoot)) {
        return $null
    }

    $matches = Get-ChildItem -Path $vsRoot -Directory -ErrorAction SilentlyContinue |
        ForEach-Object {
            Get-ChildItem -Path $_.FullName -Directory -ErrorAction SilentlyContinue
        } |
        ForEach-Object {
            $candidate = Join-Path $_.FullName $relativePath
            if (Test-Path -LiteralPath $candidate) {
                Get-Item -LiteralPath $candidate
            }
        } |
        Sort-Object LastWriteTime -Descending

    if ($matches) {
        return $matches[0].FullName
    }
    return $null
}

$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
} else {
    $cmakePath = Find-VisualStudioTool(
        'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
}
if (-not $cmakePath) {
    throw 'CMake was not found. Install CMake or the Visual Studio C++ CMake tools.'
}

# Import VsDevCmd's environment into this PowerShell process when cl.exe is
# not already available. This keeps the script non-interactive and local to
# the current process; it does not change the user's system environment.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vsDevCmd = Find-VisualStudioTool('Common7\Tools\VsDevCmd.bat')
    if (-not $vsDevCmd) {
        throw 'Visual Studio C++ build tools were not found.'
    }

    $devEnvironmentCommand =
        "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set"
    $environmentLines = & cmd.exe /d /s /c $devEnvironmentCommand
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd failed with exit code $LASTEXITCODE."
    }

    # Some parent environments contain both PATH and Path entries. `set`
    # returns both, so select the entry VsDevCmd actually updated (it contains
    # the MSVC tool directory) instead of relying on name casing or order.
    $pathLines = $environmentLines | Where-Object {
        $separator = $_.IndexOf('=')
        $separator -gt 0 -and
            $_.Substring(0, $separator).Equals(
                'Path', [StringComparison]::OrdinalIgnoreCase)
    }
    $developerPath = $pathLines |
        Where-Object { $_ -match '[\\/]VC[\\/]Tools[\\/]MSVC[\\/]' } |
        Select-Object -First 1
    if (-not $developerPath) {
        $developerPath = $pathLines | Select-Object -First 1
    }
    if ($developerPath) {
        $pathSeparator = $developerPath.IndexOf('=')
        [Environment]::SetEnvironmentVariable(
            'Path', $developerPath.Substring($pathSeparator + 1), 'Process')
    }

    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            if ($name.Equals('Path', [StringComparison]::OrdinalIgnoreCase)) {
                continue
            }
            [Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }
}

$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestPath)) {
    throw "CTest was not found next to CMake: $ctestPath"
}

# Keep MSVC's /showIncludes prefix in the form Ninja recognizes. On a
# localized Visual Studio installation the translated prefix can otherwise
# flood test output with every transitive header path.
[Environment]::SetEnvironmentVariable('VSLANG', '1033', 'Process')

Push-Location $repoRoot
try {
    Write-Host "Configuring: $configurePreset" -ForegroundColor Cyan
    & $cmakePath --preset $configurePreset -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }

    Write-Host "Building: $buildPreset" -ForegroundColor Cyan
    & $cmakePath --build --preset $buildPreset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with exit code $LASTEXITCODE."
    }

    $testDirectory = Join-Path $repoRoot "build\$presetSuffix"
    Write-Host 'Running tests' -ForegroundColor Cyan
    & $ctestPath --test-dir $testDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "CTest failed with exit code $LASTEXITCODE."
    }

    Write-Host 'All automated tests passed.' -ForegroundColor Green
}
finally {
    Pop-Location
}
