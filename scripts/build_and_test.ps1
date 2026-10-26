# Builds every variant and runs all available tests.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\build_and_test.ps1 `
#            -Zig C:\path\zig.exe -Python C:\path\python.exe [-GameExe C:\path\forza_durango_release_final.exe]
param(
    [Parameter(Mandatory = $true)][string]$Zig,
    [Parameter(Mandatory = $true)][string]$Python,
    [string]$GameExe,
    [string]$Output = (Join-Path (Split-Path -Parent $PSScriptRoot) "..\out")
)
$ErrorActionPreference = "Stop"
$source = Split-Path -Parent $PSScriptRoot
$output = [IO.Path]::GetFullPath($Output)
$work = Join-Path $output ".build"

function Step($name, [scriptblock]$command) {
    Write-Host "== $name"
    & $command
    if ($LASTEXITCODE -ne 0) { throw "$name failed with exit code $LASTEXITCODE" }
}

Step "Azimuth winmm.dll" { & $Python -I (Join-Path $source "build.py") $Zig --azimuth --output (Join-Path $output "Azimuth\winmm.dll") --build-dir (Join-Path $work "azimuth") }
Step "Mezcal xg.dll" { & $Python -I (Join-Path $source "build.py") $Zig --output (Join-Path $output "Mezcal\xg.dll") --build-dir (Join-Path $work "mezcal") }
Step "Test DLL" { & $Python -I (Join-Path $source "build.py") $Zig --azimuth --test --output (Join-Path $work "test\fh2-extra-options-test.dll") --build-dir (Join-Path $work "azimuth-test") }

$hostTest = Join-Path $work "air_suspension_test.exe"
$soundTest = Join-Path $work "air_sound_test.exe"
$gestureTest = Join-Path $work "pad_gesture_test.exe"
Push-Location $source
try {
    Step "Controller test build" { & $Zig cc -std=c11 -Wall -Wextra -Werror -Wno-division-by-zero tests/air_suspension_test.c features/air_suspension.c -o $hostTest }
    Step "Gesture test build" { & $Zig cc -std=c11 -Wall -Wextra -Werror tests/pad_gesture_test.c features/pad_gesture.c -o $gestureTest }
    Step "Sound test build" { & $Zig cc -std=c11 -Wall -Wextra -Werror tests/air_sound_test.c features/air_sound_synth.c features/air_suspension.c -o $soundTest }
} finally { Pop-Location }
Step "Controller tests" { & $hostTest }
Step "Gesture tests" { & $gestureTest }
# Also writes the built-in sounds as WAV files to listen to.
Step "Sound tests" { & $soundTest $output }

if ($GameExe) {
    Step "DLL integration tests" { & $Python -I (Join-Path $source "tests\run_dll_tests.py") (Join-Path $work "test\fh2-extra-options-test.dll") $GameExe }
} else {
    Write-Host "== DLL integration tests skipped (pass -GameExe to run them)"
}
Write-Host "All builds and tests finished. Outputs in $output"
