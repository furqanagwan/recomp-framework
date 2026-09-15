<#
.SYNOPSIS
Packages a built game into a release zip and, with -Publish, creates the GitHub release.

.DESCRIPTION
Run from the game repository root after building the game with build.ps1. The zip
contains the executable, the ReXGlue runtime DLLs, the default settings file, a
player README and the license files. Game data is never included.

Release details (supported disc, system requirements, publisher) come from
<Game>\release.json, for example:

{
  "supported_disc": "Fight Night Round 4 (USA, Europe), title ID 45410894",
  "publisher": "Electronic Arts",
  "system_requirements": [ "OS: Windows 10 version 2004 or later, 64-bit" ]
}

.EXAMPLE
.\framework\scripts\package_release.ps1 -Game fightNight4 -Version 0.1.0
.\framework\scripts\package_release.ps1 -Game fightNight4 -Version 0.1.0 -Publish
#>
param(
    [Parameter(Mandatory)][string]$Game,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$')][string]$Version,
    [string]$Preset = "win-amd64-release",
    [switch]$Publish,
    [switch]$Draft
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = & (Join-Path $PSScriptRoot 'repository_root.ps1')
$frameworkRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$gameRoot = Join-Path $repositoryRoot $Game
$buildDir = Join-Path $gameRoot "out\build\$Preset"
$releaseConfigPath = Join-Path $gameRoot 'release.json'

if (-not (Test-Path (Join-Path $gameRoot 'CMakeLists.txt'))) { throw "No game project at $gameRoot" }
if (-not (Test-Path $releaseConfigPath)) { throw "Missing $releaseConfigPath (see the help for its format)" }
$releaseConfig = Get-Content $releaseConfigPath -Raw | ConvertFrom-Json

$cmakeText = Get-Content (Join-Path $gameRoot 'CMakeLists.txt') -Raw
if ($cmakeText -notmatch 'project\((\w+)') { throw "Could not read the project name from CMakeLists.txt" }
$projectName = $Matches[1]
if ($cmakeText -notmatch 'OUTPUT_NAME "([^"]+)"') { throw "Could not read OUTPUT_NAME from CMakeLists.txt" }
$displayName = $Matches[1]

$executable = Join-Path $buildDir "$displayName.exe"
if (-not (Test-Path $executable)) { throw "Build the game first: $executable not found" }

$dirty = git -C $repositoryRoot status --porcelain --untracked-files=no
if ($dirty) { Write-Warning "The repository has uncommitted changes; the release will not match a commit." }
$commit = (git -C $repositoryRoot rev-parse --short HEAD).Trim()

$remote = (git -C $repositoryRoot remote get-url origin).Trim() -replace '\.git$', ''
$projectUrl = "$remote/tree/main/$Game"

$archiveName = ($displayName -replace '[^A-Za-z0-9]+', '') + "-v$Version-windows-x64"
$releaseOut = Join-Path $gameRoot 'out\release'
$stage = Join-Path $releaseOut $archiveName
$zipPath = Join-Path $releaseOut "$archiveName.zip"
Remove-Item $stage, $zipPath -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $executable $stage
foreach ($runtimeFile in 'rexruntime.dll', 'rexgpu-xenos.dll') {
    $source = Join-Path $buildDir $runtimeFile
    if (-not (Test-Path $source)) { throw "Missing $source" }
    Copy-Item $source $stage
}
$settings = Join-Path $buildDir "$projectName.toml"
if (Test-Path $settings) { Copy-Item $settings $stage }

Copy-Item (Join-Path $repositoryRoot 'LICENSE') (Join-Path $stage 'LICENSE.txt')
$notices = @(
    "ReXGlue SDK (https://github.com/furqanagwan/rexglue-sdk)",
    "",
    (Get-Content (Join-Path $frameworkRoot 'thirdparty\rexglue-sdk\LICENSE') -Raw),
    "",
    "recomp-framework (https://github.com/furqanagwan/recomp-framework)",
    "",
    (Get-Content (Join-Path $frameworkRoot 'LICENSE') -Raw)
) -join "`r`n"
[IO.File]::WriteAllText((Join-Path $stage 'THIRD-PARTY-NOTICES.txt'), $notices)

$requirements = ($releaseConfig.system_requirements | ForEach-Object { "- $_" }) -join "`r`n"
$readme = Get-Content (Join-Path $frameworkRoot 'templates\release\README.txt') -Raw
$tokens = @{
    '@DISPLAY_NAME@'        = $displayName
    '@VERSION@'             = "v$Version"
    '@SUPPORTED_DISC@'      = $releaseConfig.supported_disc
    '@SYSTEM_REQUIREMENTS@' = $requirements
    '@PROJECT_URL@'         = $projectUrl
    '@PUBLISHER@'           = $releaseConfig.publisher
}
foreach ($token in $tokens.Keys) { $readme = $readme.Replace($token, $tokens[$token]) }
[IO.File]::WriteAllText((Join-Path $stage 'README.txt'), ($readme -replace "(?<!`r)`n", "`r`n"))

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath
$hash = (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLower()
Write-Host "Packaged $zipPath"
Write-Host "SHA-256 $hash"

if (-not $Publish) { return }

$tag = "$Game-v$Version"
$notes = @"
$displayName v$Version, built from $commit.

Download **$archiveName.zip**, extract it and run ``$displayName.exe``. You need your own Xbox 360 disc image.

**Supported disc:** $($releaseConfig.supported_disc)

**System requirements**

$(($releaseConfig.system_requirements | ForEach-Object { "- $_" }) -join "`n")

See the [game README]($projectUrl) for status and known issues.

SHA-256: ``$hash``
"@
$notesPath = Join-Path $releaseOut "$archiveName-notes.md"
[IO.File]::WriteAllText($notesPath, $notes)

$arguments = @('release', 'create', $tag, $zipPath, '--title', "$displayName v$Version", '--notes-file', $notesPath, '--target', (git -C $repositoryRoot rev-parse HEAD).Trim())
if ($Draft) { $arguments += '--draft' }
if ($Version -match '-') { $arguments += '--prerelease' }
gh @arguments
if ($LASTEXITCODE -ne 0) { throw "gh release create failed" }
