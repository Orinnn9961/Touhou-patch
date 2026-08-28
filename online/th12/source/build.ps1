param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $PSScriptRoot "build"
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
    throw "Visual Studio x86 C++ tools were not found."
}

cmake -S $PSScriptRoot -B $build -G "Visual Studio 17 2022" -A Win32
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE."
}
cmake --build $build --config $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
}

$out = Join-Path $root "coop-bin"
New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item (Join-Path $build "$Configuration\coop-launcher.exe") (Join-Path $out "coop-launcher.exe") -Force
Copy-Item (Join-Path $build "$Configuration\dinput8.dll") (Join-Path $out "dinput8.dll") -Force
Copy-Item (Join-Path $build "$Configuration\th12_coop.dll") (Join-Path $out "th12_coop.dll") -Force
Copy-Item (Join-Path $build "$Configuration\proxy-smoke.exe") (Join-Path $out "proxy-smoke.exe") -Force
Copy-Item (Join-Path $build "$Configuration\player_context_tests.exe") (Join-Path $out "player-context-tests.exe") -Force
Copy-Item (Join-Path $build "$Configuration\resource_bank_tests.exe") (Join-Path $out "resource-bank-tests.exe") -Force
Copy-Item (Join-Path $build "$Configuration\player2_input_tests.exe") (Join-Path $out "player2-input-tests.exe") -Force
Copy-Item (Join-Path $build "$Configuration\frame_input_tests.exe") (Join-Path $out "frame-input-tests.exe") -Force
Copy-Item (Join-Path $build "$Configuration\script_input_tests.exe") (Join-Path $out "script-input-tests.exe") -Force
Copy-Item (Join-Path $build "$Configuration\lockstep_timeline_tests.exe") (Join-Path $out "lockstep-timeline-tests.exe") -Force
Copy-Item (Join-Path $build "$Configuration\network_protocol_tests.exe") (Join-Path $out "network-protocol-tests.exe") -Force
Copy-Item (Join-Path $build "$Configuration\lan_session_tests.exe") (Join-Path $out "lan-session-tests.exe") -Force
Copy-Item (Join-Path $build "$Configuration\determinism_trace_tests.exe") (Join-Path $out "determinism-trace-tests.exe") -Force
Copy-Item (Join-Path $build "$Configuration\collision_ownership_tests.exe") (Join-Path $out "collision-ownership-tests.exe") -Force
Copy-Item (Join-Path $build "$Configuration\boss_health_tests.exe") (Join-Path $out "boss-health-tests.exe") -Force
Copy-Item (Join-Path $build "$Configuration\phase7-compare.exe") (Join-Path $out "phase7-compare.exe") -Force
Copy-Item (Join-Path $build "$Configuration\sht-inspect.exe") (Join-Path $out "sht-inspect.exe") -Force
Write-Host "Built TH12 co-op artifacts in $out"
