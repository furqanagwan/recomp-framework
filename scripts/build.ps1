<#
.SYNOPSIS
Configures and builds a game.

.EXAMPLE
.\framework\scripts\build.ps1 -Game fightNight4
.\framework\scripts\build.ps1 -Game topspin4 -Preset win-amd64-relwithdebinfo
.\framework\scripts\build.ps1 -Game skate2 -CMakeArgs '-DCMAKE_EXE_LINKER_FLAGS=-Wl,/DEBUG'
#>
param(
    [Parameter(Mandatory)][string]$Game,
    [string]$Preset = "win-amd64-release",
    # Build against a rexglue-sdk source tree instead of the installed SDK.
    [string]$SdkDir = "",
    # Build one target (or object file) instead of everything.
    [string]$Target = "",
    # Extra arguments for the configure step, e.g. linker flags for symbols.
    [string[]]$CMakeArgs = @()
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = & (Join-Path $PSScriptRoot 'repository_root.ps1')
$gameRoot = Join-Path $repositoryRoot $Game
if (-not (Test-Path (Join-Path $gameRoot 'CMakeLists.txt'))) { throw "No game project at $gameRoot" }

function Enter-DeveloperEnvironment {
    if ($env:VSCMD_VER) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return }
    $visualStudio = & $vswhere -latest -property installationPath
    Import-Module (Join-Path $visualStudio 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $visualStudio -Arch amd64 -HostArch amd64 -SkipAutomaticLocation -DevCmdArguments '-no_logo' | Out-Null
    $llvm = Join-Path $env:ProgramFiles 'LLVM\bin'
    if (Test-Path $llvm) { $env:Path = "$llvm;$env:Path" }
}

Enter-DeveloperEnvironment

$configureArguments = @('--preset', $Preset) + $CMakeArgs
if ($SdkDir) { $configureArguments += "-DREXSDK_DIR=$(Resolve-Path $SdkDir)" }
$buildArguments = @('--build', (Join-Path 'out\build' $Preset))
if ($Target) { $buildArguments += @('--target', $Target) }

Push-Location $gameRoot
try {
    # CMake and codegen log to stderr. Windows PowerShell 5.1 turns redirected
    # stderr into errors, which 'Stop' would make fatal, so rely on exit codes.
    $ErrorActionPreference = 'Continue'
    # Empty stdin: rc.exe waits forever on an inherited console input handle when
    # the build runs from a non-interactive shell (CI, scripts, agents).
    $null | cmake @configureArguments
    if ($LASTEXITCODE -ne 0) { throw "Configure failed" }
    $null | cmake @buildArguments
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}
finally {
    Pop-Location
}
