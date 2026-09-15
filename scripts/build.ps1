param(
    [Parameter(Mandatory)][string]$Game,
    [string]$Preset = "win-amd64-release",
    [string]$SdkDir = ""
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

$configureArguments = @('--preset', $Preset)
if ($SdkDir) { $configureArguments += "-DREXSDK_DIR=$(Resolve-Path $SdkDir)" }

Push-Location $gameRoot
try {
    # CMake and codegen log to stderr. Windows PowerShell 5.1 turns redirected
    # stderr into errors, which 'Stop' would make fatal, so rely on exit codes.
    $ErrorActionPreference = 'Continue'
    cmake @configureArguments
    if ($LASTEXITCODE -ne 0) { throw "Configure failed" }
    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}
finally {
    Pop-Location
}
