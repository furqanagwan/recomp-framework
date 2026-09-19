param(
    [Parameter(Mandatory)][string]$Game,
    [Parameter(Mandatory)][string]$ProjectName,
    [string]$ArtworkFile = "metadata\gdk_hd\title_1024.png"
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$repositoryRoot = & (Join-Path $PSScriptRoot 'repository_root.ps1')
$gameRoot = Join-Path $repositoryRoot $Game
$metadata = Join-Path $gameRoot 'metadata'
$visuals = Join-Path $metadata 'gdk_hd'
$artworkPath = Join-Path $gameRoot $ArtworkFile
$iconSizes = 16, 24, 32, 48, 64, 128, 256
$shellVisuals = [ordered]@{
    'Logo.png'         = @(150, 150)
    'StoreLogo.png'    = @(100, 100)
    'SmallLogo.png'    = @(44, 44)
    'LargeLogo.png'    = @(480, 480)
    'SplashScreen.png' = @(1920, 1080)
}

function New-CenteredBitmap([System.Drawing.Image]$artwork, [int]$width, [int]$height) {
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::Black)
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $side = [Math]::Min($width, $height)
        if ($width -ne $height) { $side = [int]($side / 2) }
        $graphics.DrawImage($artwork, [int](($width - $side) / 2), [int](($height - $side) / 2), $side, $side)
    }
    finally {
        $graphics.Dispose()
    }
    $bitmap
}

function Get-PngBytes([System.Drawing.Bitmap]$bitmap) {
    $stream = New-Object System.IO.MemoryStream
    try {
        $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        , $stream.ToArray()
    }
    finally {
        $stream.Dispose()
    }
}

function Write-IconFile([System.Drawing.Image]$artwork, [string]$destination) {
    $images = foreach ($size in $iconSizes) {
        $bitmap = New-CenteredBitmap $artwork $size $size
        try { , (Get-PngBytes $bitmap) } finally { $bitmap.Dispose() }
    }
    $stream = [System.IO.File]::Create($destination)
    $writer = New-Object System.IO.BinaryWriter $stream
    try {
        $writer.Write([UInt16]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]$iconSizes.Count)
        $offset = 6 + 16 * $iconSizes.Count
        for ($index = 0; $index -lt $iconSizes.Count; $index++) {
            $size = $iconSizes[$index]
            $writer.Write([byte]($(if ($size -ge 256) { 0 } else { $size })))
            $writer.Write([byte]($(if ($size -ge 256) { 0 } else { $size })))
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]32)
            $writer.Write([UInt32]$images[$index].Length)
            $writer.Write([UInt32]$offset)
            $offset += $images[$index].Length
        }
        foreach ($image in $images) { $writer.Write($image) }
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

if (-not (Test-Path $artworkPath)) { throw "Missing $artworkPath (upscale metadata\icons\title.png first)" }
New-Item -ItemType Directory -Force $visuals | Out-Null
$artwork = [System.Drawing.Image]::FromFile($artworkPath)
try {
    Write-IconFile $artwork (Join-Path $metadata "$ProjectName.ico")
    foreach ($visual in $shellVisuals.GetEnumerator()) {
        $bitmap = New-CenteredBitmap $artwork $visual.Value[0] $visual.Value[1]
        try { $bitmap.Save((Join-Path $visuals $visual.Key), [System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
    }
}
finally {
    $artwork.Dispose()
}
Write-Host "Wrote $ProjectName.ico and shell visuals to $metadata"
