# Build RUMOR Standalone (+ VST3) on Windows with MSVC.
# Run from "x64 Native Tools" or Developer PowerShell for VS 2022.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build" }

cmake -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
  -DRUMOR_WITH_PAULXSTRETCH=ON `
  -DRUMOR_BUILD_PLUGIN=ON `
  -DRUMOR_BUILD_TESTS=ON

cmake --build $BuildDir --config Release --target Rumor_Standalone

$Exe = Join-Path $BuildDir "Rumor_artefacts\Release\Standalone\RUMOR.exe"
if (Test-Path $Exe) {
  Write-Host "OK: $Exe"
} else {
  Write-Host "Build finished; locate RUMOR.exe under $BuildDir\Rumor_artefacts"
}
