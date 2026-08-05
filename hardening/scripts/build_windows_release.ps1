$ErrorActionPreference = "Stop"

$Root = (git rev-parse --show-toplevel).Trim()
Set-Location $Root

cmake --preset windows-x86_64-release
cmake --build --preset windows-x86_64-release -j 2
ctest --preset windows-x86_64-release
python hardening/scripts/sync_smoke_addon.py --repo $Root

$Godot = $env:GODOT_EXECUTABLE
if ($Godot -and (Test-Path $Godot)) {
    & $Godot --headless --path hardening/smoke
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $Godot --headless --path hardening/benchmark
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Host "GODOT_EXECUTABLE is not set; native build/tests completed and smoke/benchmark were skipped."
}
