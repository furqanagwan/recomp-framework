param(
    [Parameter(Mandatory)][string]$Folder,
    [Parameter(Mandatory)][string]$ProjectName,
    [Parameter(Mandatory)][string]$DisplayName,
    [Parameter(Mandatory)][string]$ReleaseYear,
    # Original publisher, shown in the Xbox app package metadata.
    [string]$Publisher = "Electronic Arts",
    # Short label that opens the store description, e.g. "EA SPORTS basketball".
    [string]$Label = "EA SPORTS",
    [string]$Rexglue = "rexglue"
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = & (Join-Path $PSScriptRoot 'repository_root.ps1')
$templateRoot = Join-Path $PSScriptRoot '..\templates\game'
$gameRoot = Join-Path $repositoryRoot $Folder
$entrypoint = Join-Path $gameRoot 'assets\default.xex'

if ($ProjectName -notmatch '^[a-z][a-z0-9_]*$') { throw "ProjectName must be snake_case, e.g. nba_live_10" }
if (-not (Test-Path $entrypoint)) { throw "Extract the game disc into $gameRoot\assets first (default.xex missing)." }
if (Test-Path (Join-Path $gameRoot 'CMakeLists.txt')) { throw "$Folder already contains a game project." }

$appClass = (($ProjectName -split '_') | ForEach-Object { $_.Substring(0, 1).ToUpper() + $_.Substring(1) }) -join ''
$appClass += 'App'
$identityAlias = ($DisplayName -replace '[^A-Za-z0-9]', '')
$replacements = @{
    '@PROJECT_NAME@'   = $ProjectName
    '@DISPLAY_NAME@'   = $DisplayName
    '@APP_CLASS@'      = $appClass
    '@IDENTITY_NAME@'  = "$($Publisher -replace '[^A-Za-z0-9]', '').$($identityAlias.ToUpper())"
    '@IDENTITY_ALIAS@' = $identityAlias
    '@RELEASE_YEAR@'   = $ReleaseYear
    '@PUBLISHER@'      = $Publisher
    '@LABEL@'          = $Label
    '@FOLDER@'         = $Folder
}
$destinations = @{
    'CMakeLists.txt'           = 'CMakeLists.txt'
    'CMakePresets.json'        = 'CMakePresets.json'
    'uwp\AppxManifest.xml'     = 'uwp\AppxManifest.xml'
    'src\main.cpp'             = 'src\main.cpp'
    'src\app.h'                = "src\$($ProjectName)_app.h"
    'settings\game.toml'       = "settings\$ProjectName.toml"
    'config\codegen.toml'      = 'config\codegen.toml'
    'config\functions.toml'    = 'config\functions.toml'
    'gdk\MicrosoftGame.config' = 'gdk\MicrosoftGame.config'
    'resources\game.rc'        = "resources\$ProjectName.rc"
    'README.md'                = 'README.md'
    'release.json'             = 'release.json'
    'docs\NOTES.md'            = 'docs\NOTES.md'
}

function Write-Template([string]$source, [string]$destination) {
    $text = [IO.File]::ReadAllText((Join-Path $templateRoot $source))
    foreach ($token in $replacements.Keys) { $text = $text.Replace($token, $replacements[$token]) }
    $target = Join-Path $gameRoot $destination
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    [IO.File]::WriteAllText($target, $text.Replace("`r`n", "`n"))
}

# rexglue logs to stderr. Windows PowerShell 5.1 turns redirected stderr into
# errors, which 'Stop' would make fatal, so rely on the exit code instead.
$ErrorActionPreference = 'Continue'
& $Rexglue init --project-name $ProjectName --project-root $gameRoot `
    --xex-path $entrypoint --game-root (Join-Path $gameRoot 'assets') --scan-dll
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw "rexglue init failed" }

Remove-Item (Join-Path $gameRoot 'src') -Recurse -Force -ErrorAction SilentlyContinue
foreach ($source in $destinations.Keys) { Write-Template $source $destinations[$source] }

$manifest = Join-Path $gameRoot "$($ProjectName)_manifest.toml"
# The executable's includes come first. DLL modules share only codegen.toml: their seeds
# are other addresses, and the analysis scripts give each module its own functions.toml.
$emptyIncludes = [regex]'includes = \[\]'
$manifestText = [IO.File]::ReadAllText($manifest)
$manifestText = $emptyIncludes.Replace($manifestText, "includes = [`n    `"config/codegen.toml`",`n    `"config/functions.toml`",`n]", 1)
$manifestText = $emptyIncludes.Replace($manifestText, "includes = [`n    `"config/codegen.toml`",`n]")
[IO.File]::WriteAllText($manifest, $manifestText.Replace("`r`n", "`n"))

Write-Host "Created $Folder ($DisplayName). Next: .\framework\scripts\discover_functions.ps1 -Game $Folder, then fill in the README TODOs"
