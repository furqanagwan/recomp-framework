param(
    [Parameter(Mandatory)][string]$Game,
    [string]$Preset = "win-amd64-release",
    [string]$VisualsDir = "metadata\gdk_hd",
    [switch]$Register,
    [switch]$Unregister,
    [switch]$Pack,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = & (Join-Path $PSScriptRoot 'repository_root.ps1')
$gameRoot = Join-Path $repositoryRoot $Game
$gdkBin = Join-Path ${env:ProgramFiles(x86)} 'Microsoft GDK\bin'
$makepkg = Join-Path $gdkBin 'makepkg.exe'
$wdapp = Join-Path $gdkBin 'wdapp.exe'
$buildDir = Join-Path $gameRoot "out\build\$Preset"
$gdkOut = Join-Path $gameRoot 'out\gdk'
$layout = Join-Path $gdkOut 'layout'
$packageDir = Join-Path $gdkOut 'package'
$mapFile = Join-Path $gdkOut 'layout.xml'
$gameConfigPath = Join-Path $gameRoot 'gdk\MicrosoftGame.config'
$shellVisuals = 'StoreLogo.png', 'Logo.png', 'SmallLogo.png', 'LargeLogo.png', 'SplashScreen.png'

function Assert-DeveloperMode {
    $unlock = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -ErrorAction SilentlyContinue
    if ($unlock.AllowDevelopmentWithoutDevLicense -ne 1) {
        throw "Windows Developer Mode is off. Enable it in Settings > System > For developers."
    }
}

function Get-GameConfig {
    [xml](Get-Content $gameConfigPath)
}

function New-Layout([string]$executableName) {
    if (Test-Path $layout) { Remove-Item $layout -Recurse -Force }
    New-Item -ItemType Directory -Force $layout | Out-Null
    Copy-Item (Join-Path $buildDir $executableName) $layout
    Get-ChildItem $buildDir -Filter *.dll | Copy-Item -Destination $layout
    Get-ChildItem $buildDir -Filter *.toml | Copy-Item -Destination $layout
    Copy-Item $gameConfigPath $layout
    foreach ($visual in $shellVisuals) {
        $source = Join-Path $gameRoot "$VisualsDir\$visual"
        if (-not (Test-Path $source)) { throw "Missing $source" }
        Copy-Item $source $layout
    }
}

function New-Package {
    New-Item -ItemType Directory -Force $packageDir | Out-Null
    Get-ChildItem $packageDir -Filter *.msixvc -ErrorAction SilentlyContinue | Remove-Item -Force
    & $makepkg genmap /f $mapFile /d $layout
    if ($LASTEXITCODE -ne 0) { throw "makepkg genmap failed" }
    & $makepkg pack /f $mapFile /lt /d $layout /nogameos /pc /pd $packageDir
    if ($LASTEXITCODE -ne 0) { throw "makepkg pack failed" }
}

function Install-Package([string]$identityName) {
    $package = Get-ChildItem $packageDir -Filter *.msixvc | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $package) { throw "No package in $packageDir" }
    # wdapp uninstall wants the full name, not the family name: given the latter
    # it prints "Parameter should be a PackageFullName", leaves the package in
    # place, and the install that follows fails with 0x80073cfb because the
    # package is still there. It says so on stdout rather than in its exit code,
    # so the removal is checked by looking again.
    $installed = Get-AppxPackage -Name $identityName -ErrorAction SilentlyContinue
    if ($installed) {
        & $wdapp uninstall $installed.PackageFullName
        if (Get-AppxPackage -Name $identityName -ErrorAction SilentlyContinue) {
            throw "Could not remove the installed $identityName. A layout registered with -Register is removed with -Unregister."
        }
    }
    & $wdapp install $package.FullName
    if ($LASTEXITCODE -ne 0) { throw "wdapp install failed" }
}

if (-not (Test-Path $makepkg)) { throw "Microsoft GDK not found (winget install Microsoft.Gaming.GDK)" }
$gameConfig = Get-GameConfig
$executableName = $gameConfig.Game.ExecutableList.Executable.Name
$identityName = $gameConfig.Game.Identity.Name

if ($Unregister) {
    Assert-DeveloperMode
    & $wdapp unregister $layout
    exit $LASTEXITCODE
}

if (-not (Test-Path (Join-Path $buildDir $executableName))) { throw "Build $Game with preset $Preset first." }
New-Layout $executableName

if ($Register) {
    Assert-DeveloperMode
    & $wdapp register $layout
    if ($LASTEXITCODE -ne 0) { throw "wdapp register failed" }
}
if ($Pack -or $Install) { New-Package }
if ($Install) {
    Assert-DeveloperMode
    Install-Package $identityName
}
