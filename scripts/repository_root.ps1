# Prints the game repository that contains this framework checkout.
# Game repositories add the framework as a submodule at <repository>\framework,
# so the repository is two levels above this script. Set RECOMP_REPOSITORY_ROOT
# to use a different layout.
if ($env:RECOMP_REPOSITORY_ROOT) {
    (Resolve-Path $env:RECOMP_REPOSITORY_ROOT).Path
}
else {
    (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}
