<#
.SYNOPSIS
Launches a built game for a fixed time and writes a run report.

.DESCRIPTION
Starts the executable, takes window screenshots at the given times, and stops
it when the time is up. The report in <Game>\out\runs\<timestamp>\ holds:

  summary.md / summary.json   outcome, crash location, errors and top warnings
  screenshot-<s>s.png         the game window at each requested time
  game.log                    the run's log file

When the game crashes, the Windows crash record names the faulting module and
offset. If that module has symbols (a RelWithDebInfo preset, or a build with
-CMakeArgs '-DCMAKE_EXE_LINKER_FLAGS=-Wl,/DEBUG'), the offset is mapped to the
generated function and line with llvm-symbolizer.

summary.json is stable, so two runs (for example before and after an SDK
change) can be compared with compare_runs.py.

.EXAMPLE
.\framework\scripts\run_game.ps1 -Game skate2
.\framework\scripts\run_game.ps1 -Game topspin4 -Preset win-amd64-relwithdebinfo -Seconds 60 -Screenshots 10,30,55 -GameArgs '--log_level=debug'
#>
param(
    [Parameter(Mandatory)][string]$Game,
    [string]$Preset = "win-amd64-release",
    [int]$Seconds = 90,
    [int[]]$Screenshots = @(20, 50, 85),
    [string[]]$GameArgs = @()
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = & (Join-Path $PSScriptRoot 'repository_root.ps1')
$gameRoot = Join-Path $repositoryRoot $Game
$buildDir = Join-Path $gameRoot "out\build\$Preset"
$executable = Get-ChildItem (Join-Path $buildDir '*.exe') -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $executable) { throw "Build the game first: no executable in $buildDir" }

$runDir = Join-Path $gameRoot ("out\runs\" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $runDir | Out-Null

Add-Type -AssemblyName System.Drawing
if (-not ('RecompRunWindow' -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class RecompRunWindow {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
}
"@
}

function Save-WindowScreenshot([System.Diagnostics.Process]$process, [string]$path) {
    $process.Refresh()
    $rect = New-Object RecompRunWindow+Rect
    if ($process.MainWindowHandle -eq [IntPtr]::Zero -or
        -not [RecompRunWindow]::GetWindowRect($process.MainWindowHandle, [ref]$rect)) { return $false }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) { return $false }
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    return $true
}

function Resolve-CrashLocation([string]$moduleName, [uint64]$offset) {
    $binary = Get-ChildItem $buildDir -File | Where-Object { $_.Name -ieq $moduleName } | Select-Object -First 1
    $llvm = Join-Path $env:ProgramFiles 'LLVM\bin'
    if (-not $binary -or -not (Test-Path (Join-Path $llvm 'llvm-symbolizer.exe'))) { return $null }
    if (-not (Test-Path ([IO.Path]::ChangeExtension($binary.FullName, '.pdb')))) { return $null }
    $imageBaseLine = & (Join-Path $llvm 'llvm-objdump.exe') -p $binary.FullName | Select-String 'ImageBase' | Select-Object -First 1
    if (-not $imageBaseLine) { return $null }
    $imageBase = [Convert]::ToUInt64(($imageBaseLine.Line -split '\s+')[-1], 16)
    $address = '0x{0:X}' -f ($imageBase + $offset)
    $lines = & (Join-Path $llvm 'llvm-symbolizer.exe') --inlining "--obj=$($binary.FullName)" $address |
        Where-Object { $_ -and $_ -ne '??' -and $_ -notmatch '^\?\?:0' }
    if (-not $lines) { return $null }
    return ($lines -join ' <- ')
}

$started = Get-Date
$launch = @{ FilePath = $executable.FullName; WorkingDirectory = $buildDir; PassThru = $true }
if ($GameArgs) { $launch.ArgumentList = $GameArgs }  # Start-Process rejects an empty list
$process = Start-Process @launch
$null = $process.Handle  # keep the handle so ExitCode is available after exit
$shots = @()
foreach ($time in ($Screenshots | Where-Object { $_ -lt $Seconds } | Sort-Object)) {
    $wait = $time - ((Get-Date) - $started).TotalSeconds
    if ($wait -gt 0 -and $process.WaitForExit([int]($wait * 1000))) { break }
    if ($process.HasExited) { break }
    $path = Join-Path $runDir "screenshot-${time}s.png"
    if (Save-WindowScreenshot $process $path) { $shots += (Split-Path $path -Leaf) }
}
$remaining = $Seconds - ((Get-Date) - $started).TotalSeconds
$exited = $process.HasExited -or ($remaining -gt 0 -and $process.WaitForExit([int]($remaining * 1000)))
if (-not $exited) { Stop-Process -Id $process.Id -Force; $process.WaitForExit() }
$duration = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)

$log = Get-ChildItem (Join-Path $buildDir 'logs') -Filter '*.log' -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-2) } | Sort-Object LastWriteTime | Select-Object -Last 1
$logLines = @()
if ($log) {
    Copy-Item $log.FullName (Join-Path $runDir 'game.log')
    $logLines = Get-Content $log.FullName
}

$crash = $null
if ($exited) {
    Start-Sleep -Seconds 2  # Windows Error Reporting writes the record asynchronously
    $record = Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = $started } -ErrorAction SilentlyContinue |
        Where-Object { $_.Message -match [regex]::Escape($executable.Name) } | Select-Object -First 1
    if ($record) {
        $fields = @{}
        foreach ($line in ($record.Message -split "`r?`n")) {
            if ($line -match '^\s*([^:]+):\s*(.*)$') { $fields[$Matches[1].Trim()] = $Matches[2].Trim() }
        }
        $offset = [Convert]::ToUInt64(($fields['Fault offset'] -replace '^0x', ''), 16)
        $crash = [ordered]@{
            module   = $fields['Faulting module name'] -replace ',.*$', ''
            code     = $fields['Exception code']
            offset   = $fields['Fault offset']
            location = Resolve-CrashLocation ($fields['Faulting module name'] -replace ',.*$', '') $offset
        }
    }
}

function Get-Message([string]$line) { $line -replace '^\[[^\]]*\] \[[a-z]+\] \[[a-z]+\] \[t\d+\] ', '' }
function Get-Shape([string]$message) { ($message -replace '0x[0-9A-Fa-f]+', '0x.') -replace '\b[0-9A-F]{8}\b', '.' }

$fatal = @($logLines | Where-Object { $_ -match '\[(critical|error)\]' } | ForEach-Object { Get-Message $_ } | Select-Object -Unique -First 20)
$warnings = @($logLines | Where-Object { $_ -match '\[warning\]' } | ForEach-Object { Get-Shape (Get-Message $_) } |
    Group-Object | Sort-Object Count -Descending | Select-Object -First 10 | ForEach-Object { [ordered]@{ count = $_.Count; message = $_.Name } })

$outcome = if (-not $exited) { 'ran for the full time' } elseif ($crash) { 'crashed' } elseif ($process.ExitCode -eq 0) { 'exited normally' } else { 'exited with an error' }
$summary = [ordered]@{
    game        = $Game
    preset      = $Preset
    outcome     = $outcome
    seconds     = $duration
    exit_code   = if ($exited) { '0x{0:X8}' -f $process.ExitCode } else { $null }
    crash       = $crash
    log_lines   = $logLines.Count
    errors      = $fatal
    warnings    = $warnings
    screenshots = $shots
}
$summary | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $runDir 'summary.json') -Encoding utf8

$report = @("# $Game run, $(Get-Date $started -Format 'yyyy-MM-dd HH:mm')", '',
    "- Outcome: **$outcome** after $duration s ($Preset)")
if ($summary.exit_code) { $report += "- Exit code: ``$($summary.exit_code)``" }
if ($crash) {
    $report += "- Crash: ``$($crash.code)`` in ``$($crash.module)`` at offset ``$($crash.offset)``"
    $report += if ($crash.location) { "- Location: ``$($crash.location)``" } else { "- Location: no symbols; rebuild with the win-amd64-relwithdebinfo preset to resolve it" }
}
$report += "- Log: $($logLines.Count) lines$(if (-not $log) { ' (no log file found)' })"
$report += '', '## Errors', ''
$report += if ($fatal) { $fatal | ForEach-Object { "- ``$_``" } } else { '- None' }
$report += '', '## Most frequent warnings', ''
$report += if ($warnings) { $warnings | ForEach-Object { "- $($_.count) x ``$($_.message)``" } } else { '- None' }
$report += '', '## Screenshots', ''
$report += if ($shots) { $shots | ForEach-Object { "- $_" } } else { '- None (the window closed before the first one)' }
$report | Set-Content (Join-Path $runDir 'summary.md') -Encoding utf8

Write-Host "$Game`: $outcome after $duration s$(if ($crash) { ", $($crash.code) in $($crash.module)$(if ($crash.location) { " at $($crash.location)" })" })"
Write-Host "Report: $runDir"
