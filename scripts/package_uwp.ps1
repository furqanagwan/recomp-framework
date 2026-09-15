param(
    [Parameter(Mandatory)][string]$Game,
    [string]$Preset = "win-amd64-uwp-release",
    [string]$ArtworkFile = "metadata\gdk_hd\title_1024.png",
    [switch]$Register,
    [switch]$Unregister,
    [switch]$Pack
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$repositoryRoot = & (Join-Path $PSScriptRoot 'repository_root.ps1')
$gameRoot = Join-Path $repositoryRoot $Game
$buildDir = Join-Path $gameRoot "out\build\$Preset"
$uwpOut = Join-Path $gameRoot 'out\uwp'
$layout = Join-Path $uwpOut 'layout'
$manifestPath = Join-Path $gameRoot 'uwp\AppxManifest.xml'
$windowsKitBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$vcLibsPackage = Join-Path ${env:ProgramFiles(x86)} 'Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs\14.0\Appx\Retail\x64\Microsoft.VCLibs.x64.14.00.appx'

$tileSizes = [ordered]@{
    'StoreLogo.png'         = @(50, 50)
    'Square44x44Logo.png'   = @(44, 44)
    'Square150x150Logo.png' = @(150, 150)
    'Wide310x150Logo.png'   = @(310, 150)
    'SplashScreen.png'      = @(620, 300)
}

function Assert-DeveloperMode {
    $unlock = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -ErrorAction SilentlyContinue
    if ($unlock.AllowDevelopmentWithoutDevLicense -ne 1) {
        throw "Windows Developer Mode is off. Enable it in Settings > System > For developers."
    }
}

function Find-WindowsKitTool([string]$name) {
    $tool = Get-ChildItem $windowsKitBin -Directory |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName "x64\$name" } |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1
    if (-not $tool) { throw "$name not found under $windowsKitBin" }
    $tool
}

function Save-TileImage([System.Drawing.Image]$artwork, [string]$destination, [int]$width, [int]$height) {
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::Black)
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $side = [Math]::Min($width, $height)
        $left = [int](($width - $side) / 2)
        $top = [int](($height - $side) / 2)
        $graphics.DrawImage($artwork, $left, $top, $side, $side)
        $bitmap.Save($destination, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function New-Layout([string]$executableName) {
    if (Test-Path $layout) { Remove-Item $layout -Recurse -Force }
    $assets = Join-Path $layout 'Assets'
    New-Item -ItemType Directory -Force $assets | Out-Null
    Copy-Item (Join-Path $buildDir $executableName) $layout
    Get-ChildItem $buildDir -Filter *.dll | Copy-Item -Destination $layout
    Get-ChildItem $buildDir -Filter *.toml | Copy-Item -Destination $layout
    Copy-Item $manifestPath $layout

    $artworkPath = Join-Path $gameRoot $ArtworkFile
    if (-not (Test-Path $artworkPath)) { throw "Missing artwork $artworkPath" }
    $artwork = [System.Drawing.Image]::FromFile($artworkPath)
    try {
        foreach ($tile in $tileSizes.GetEnumerator()) {
            Save-TileImage $artwork (Join-Path $assets $tile.Key) $tile.Value[0] $tile.Value[1]
        }
    }
    finally {
        $artwork.Dispose()
    }
}

function Remove-RegisteredPackage([string]$identityName) {
    Get-AppxPackage -Name $identityName -ErrorAction SilentlyContinue | Remove-AppxPackage
}

function Get-SigningCertificate([string]$publisher) {
    $certificate = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq $publisher -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) } |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1
    if (-not $certificate) {
        $certificate = New-SelfSignedCertificate -Type Custom -Subject $publisher `
            -KeyUsage DigitalSignature -FriendlyName "fightNightRecomped UWP sideload" `
            -CertStoreLocation Cert:\CurrentUser\My `
            -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    }
    $certificate
}

function New-SignedPackage([string]$identityName, [string]$version, [string]$publisher) {
    $makeappx = Find-WindowsKitTool 'makeappx.exe'
    $signtool = Find-WindowsKitTool 'signtool.exe'
    $packagePath = Join-Path $uwpOut "${identityName}_${version}_x64.msix"
    & $makeappx pack /o /d $layout /p $packagePath
    if ($LASTEXITCODE -ne 0) { throw "makeappx pack failed" }

    $certificate = Get-SigningCertificate $publisher
    & $signtool sign /fd SHA256 /sha1 $certificate.Thumbprint /s My $packagePath
    if ($LASTEXITCODE -ne 0) { throw "signtool sign failed" }
    Export-Certificate -Cert $certificate -FilePath (Join-Path $uwpOut "${identityName}.cer") | Out-Null

    $dependencies = Join-Path $uwpOut 'Dependencies\x64'
    New-Item -ItemType Directory -Force $dependencies | Out-Null
    if (Test-Path $vcLibsPackage) { Copy-Item $vcLibsPackage $dependencies -Force }
    Write-Host "Package: $packagePath"
}

$manifest = [xml](Get-Content $manifestPath)
$identityName = $manifest.Package.Identity.Name
$publisher = $manifest.Package.Identity.Publisher
$version = $manifest.Package.Identity.Version
$executableName = $manifest.Package.Applications.Application.Executable

if ($Unregister) {
    Remove-RegisteredPackage $identityName
    exit 0
}

if (-not (Test-Path (Join-Path $buildDir $executableName))) { throw "Build $Game with preset $Preset first." }
New-Item -ItemType Directory -Force $uwpOut | Out-Null
New-Layout $executableName

if ($Register) {
    Assert-DeveloperMode
    Remove-RegisteredPackage $identityName
    Add-AppxPackage -Register (Join-Path $layout 'AppxManifest.xml')
    $installed = Get-AppxPackage -Name $identityName
    Write-Host "Registered $($installed.PackageFullName)"
    Write-Host "Launch: explorer.exe shell:AppsFolder\$($installed.PackageFamilyName)!App"
}
if ($Pack) {
    New-SignedPackage $identityName $version $publisher
}
