# Verify the whole local toolchain (Python training venv + ESP-IDF + components).
# Usage (from repo root):
#   pwsh -File tools/setup/verify-toolchain.ps1
#   pwsh -File tools/setup/verify-toolchain.ps1 -Build   # also run idf.py build
param(
    [switch]$Build
)

$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$venv = if ($env:EINK_VENV) { $env:EINK_VENV } else { 'C:\venv-eink' }
$py = Join-Path $venv 'Scripts\python.exe'
$idfPath = 'F:\esp\v6.1\esp-idf'
$failed = 0

Write-Host "================ Python training venv ================"
if (Test-Path $py) {
    & $py (Join-Path $PSScriptRoot 'verify_python.py')
    if ($LASTEXITCODE -ne 0) { $failed++ }
    & $py (Join-Path $root 'tools\fonts\check_fonts.py')
    if ($LASTEXITCODE -ne 0) { $failed++ }
} else {
    Write-Host "[FAIL] missing $py"
    Write-Host "       run: pwsh -File tools/setup/install-train.ps1"
    $failed++
}

Write-Host "`n================ ESP-IDF ================"
if (-not (Test-Path (Join-Path $idfPath 'export.ps1'))) {
    Write-Host "[FAIL] ESP-IDF not found at $idfPath"
    $failed++
} else {
    $env:IDF_PATH = $idfPath
    $env:PYTHONUTF8 = '1'
    . (Join-Path $idfPath 'export.ps1') | Out-Null
    $idfVersion = (idf.py --version 2>&1 | Select-Object -Last 1)
    Write-Host "idf.py: $idfVersion"
    Write-Host "python: $((python --version 2>&1))"
    Write-Host "cmake:  $((cmake --version 2>&1 | Select-Object -First 1))"
    Write-Host "ninja:  $(ninja --version 2>&1)"
    Write-Host "gcc:    $((xtensa-esp32s3-elf-gcc --version 2>&1 | Select-Object -First 1))"

    if (Test-Path (Join-Path $root 'main\idf_component.yml')) {
        Write-Host "`n== managed components =="
        Get-ChildItem (Join-Path $root 'managed_components') -Directory -ErrorAction SilentlyContinue |
            ForEach-Object {
                $manifest = Join-Path $_.FullName 'idf_component.yml'
                if (Test-Path $manifest) {
                    $ver = (Select-String -Path $manifest -Pattern '^version:' |
                        Select-Object -First 1).Line -replace '^version:\s*', ''
                    Write-Host ("[ok]   {0,-38} {1}" -f $_.Name, $ver)
                }
            }
    }

    if ($Build) {
        Write-Host "`n== idf.py build =="
        Push-Location $root
        try {
            idf.py build 2>&1 | Select-Object -Last 15
            if ($LASTEXITCODE -ne 0) { $failed++ }
        } finally {
            Pop-Location
        }
    }
}

Write-Host "`n================ Serial ports ================"
$ports = [System.IO.Ports.SerialPort]::GetPortNames()
if ($ports) { $ports | ForEach-Object { Write-Host "[ok]   $_" } }
else { Write-Host "[warn] no COM port (board unplugged?)" }

Write-Host "`n================ Result ================"
if ($failed -eq 0) { Write-Host "PASS"; exit 0 }
Write-Host "FAIL ($failed section(s))"; exit 1
