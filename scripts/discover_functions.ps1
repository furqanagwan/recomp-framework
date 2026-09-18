<#
.SYNOPSIS
Runs the function discovery workflow for a game end to end.

.DESCRIPTION
The steps from CONTRIBUTING.md "Adding a game", for the executable and every DLL
module in the manifest:

  1. stabilize codegen (seed unresolved calls, disable seeds that split functions)
  2. build, then dump each loaded image offline during codegen (falling back to
     a timed game run with older SDKs)
  3. seed functions referenced from data, found in code gaps, and built in code
  4. stabilize, prune seeds on local branch targets, stabilize again
  5. write under-counted jump tables to switch_tables.toml
  6. write the CRT setjmp/longjmp to setjmp.toml
  7. final codegen and build

A module the game never loads within the dump timeout is skipped and reported.
Run it again after fixing a crash that stopped a module from loading.

.EXAMPLE
.\framework\scripts\discover_functions.ps1 -Game topspin4
#>
param(
    [Parameter(Mandatory)][string]$Game,
    [string]$Preset = "win-amd64-release",
    [int]$DumpTimeoutSeconds = 120,
    [string]$Rexglue = "",
    # Reuse existing image dumps instead of running the game again.
    [switch]$KeepDumps
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = & (Join-Path $PSScriptRoot 'repository_root.ps1')
$gameRoot = Join-Path $repositoryRoot $Game
$analysis = Join-Path $PSScriptRoot 'analysis'
$out = Join-Path $gameRoot 'out'
$env:RECOMP_REPOSITORY_ROOT = $repositoryRoot

function Invoke-Analysis([string]$label, [string[]]$arguments) {
    Write-Host "== $label" -ForegroundColor Cyan
    $ErrorActionPreference = 'Continue'
    python (Join-Path $analysis $arguments[0]) @($arguments | Select-Object -Skip 1) 2>&1 |
        ForEach-Object { "$_" } | Select-Object -Last 6 | Write-Host
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { throw "$label failed" }
}

function Get-Modules {
    # name -> guest file the game loads ("" for the executable)
    $json = python (Join-Path $analysis 'list_modules.py') --game $Game
    if ($LASTEXITCODE -ne 0) { throw "Could not read the manifest in $gameRoot" }
    $parsed = $json | ConvertFrom-Json
    $modules = [ordered]@{}
    foreach ($property in $parsed.PSObject.Properties) { $modules[$property.Name] = $property.Value }
    $modules
}

function Get-DumpPath([string]$module) {
    if ($module -eq 'default') { Join-Path $out 'image_dump.bin' } else { Join-Path $out "image_dump_$module.bin" }
}

function Find-Rexglue {
    if ($Rexglue) { return (Resolve-Path $Rexglue).Path }
    $command = Get-Command rexglue -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    if ($env:REXGLUE_SDK_PREFIX) {
        $candidate = Join-Path $env:REXGLUE_SDK_PREFIX 'bin\rexglue.exe'
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Save-OfflineImageDumps {
    $tool = Find-Rexglue
    if (-not $tool) { return $false }
    if (-not $KeepDumps) {
        foreach ($module in $modules.Keys) { Remove-Item (Get-DumpPath $module) -ErrorAction SilentlyContinue }
    }
    $manifest = Get-ChildItem (Join-Path $gameRoot '*_manifest.toml') | Select-Object -First 1
    if (-not $manifest) { return $false }
    Write-Host "== dumping loaded images offline" -ForegroundColor Cyan
    Push-Location $gameRoot
    try {
        $ErrorActionPreference = 'Continue'
        & $tool codegen $manifest.Name --dump-images $out
        $ok = ($LASTEXITCODE -eq 0)
        $ErrorActionPreference = 'Stop'
        return $ok
    }
    finally {
        $ErrorActionPreference = 'Stop'
        Pop-Location
    }
}

function Invoke-StabilizeCodegen {
    $arguments = @('stabilize_codegen.py', '--game', $Game)
    $tool = Find-Rexglue
    if ($tool) { $arguments += @('--rexglue', $tool) }
    Invoke-Analysis 'stabilize codegen' $arguments
}

function Save-ImageDump([string]$module, [string]$guestFile) {
    $dump = Get-DumpPath $module
    if ($KeepDumps -and (Test-Path $dump)) { return $true }
    $executable = Get-ChildItem (Join-Path $out "build\$Preset\*.exe") | Select-Object -First 1
    if (-not $executable) { throw "No executable in $out\build\$Preset" }
    Remove-Item $dump -ErrorAction SilentlyContinue
    $env:RECOMP_DUMP_IMAGE = $dump
    if ($guestFile) { $env:RECOMP_DUMP_MODULE = $guestFile } else { Remove-Item env:RECOMP_DUMP_MODULE -ErrorAction SilentlyContinue }
    try {
        $process = Start-Process $executable.FullName -PassThru -WorkingDirectory $executable.DirectoryName
        if (-not $process.WaitForExit($DumpTimeoutSeconds * 1000)) { Stop-Process -Id $process.Id -Force }
    }
    finally {
        Remove-Item env:RECOMP_DUMP_IMAGE, env:RECOMP_DUMP_MODULE -ErrorAction SilentlyContinue
    }
    return (Test-Path $dump)
}

$modules = Get-Modules
Invoke-StabilizeCodegen
& (Join-Path $PSScriptRoot 'build.ps1') -Game $Game -Preset $Preset

$offlineDumped = Save-OfflineImageDumps
if (-not $offlineDumped) {
    $manifest = Get-ChildItem (Join-Path $gameRoot '*_manifest.toml') | Select-Object -First 1
    $updateBuild = $manifest -and
        (Select-String -LiteralPath $manifest.FullName -Pattern '^\s*patched_file_path\s*=' -Quiet)
    if ($updateBuild) {
        throw 'Offline image dumping failed for an update build. Check rexglue --dump-images and stage each base XEX beside its required XEXP; an installed runtime update is not used for discovery.'
    }
    Write-Warning 'Offline image dumping is unavailable or failed; falling back to game-time dumping'
}
$dumped = @()
foreach ($module in $modules.Keys) {
    $dump = Get-DumpPath $module
    if (($offlineDumped -and (Test-Path $dump)) -or (Save-ImageDump $module $modules[$module])) {
        $dumped += $module
        Write-Host "== dumped $module" -ForegroundColor Cyan
    }
    else {
        Write-Warning "$module was not dumped: the game did not load it within $DumpTimeoutSeconds s"
    }
}

foreach ($module in $dumped) {
    Invoke-Analysis "${module}: data pointers" @('find_missing_functions.py', '--game', $Game, '--module', $module, '--write')
    Invoke-Analysis "${module}: code gaps" @('find_missing_functions.py', '--game', $Game, '--module', $module, '--gaps', '--write')
    Invoke-Analysis "${module}: code-built addresses" @('find_missing_functions.py', '--game', $Game, '--module', $module, '--code-refs', '--write')
}
Invoke-StabilizeCodegen
foreach ($module in $dumped) {
    Invoke-Analysis "${module}: prune seeds" @('prune_bad_seeds.py', '--game', $Game, '--module', $module, '--image', (Get-DumpPath $module))
}
Invoke-StabilizeCodegen
foreach ($module in $dumped) {
    Invoke-Analysis "${module}: jump tables" @('find_short_switch_tables.py', '--game', $Game, '--module', $module, '--image', (Get-DumpPath $module), '--write')
}
# Needs only generated code, so it covers modules that were not dumped too.
foreach ($module in $modules.Keys) {
    Invoke-Analysis "${module}: setjmp" @('find_setjmp.py', '--game', $Game, '--module', $module, '--write')
}
Invoke-StabilizeCodegen
& (Join-Path $PSScriptRoot 'build.ps1') -Game $Game -Preset $Preset

$skipped = @($modules.Keys | Where-Object { $_ -notin $dumped })
Write-Host "Done. Discovered: $($dumped -join ', ')$(if ($skipped) { ". Not loaded, run again later: $($skipped -join ', ')" })"
