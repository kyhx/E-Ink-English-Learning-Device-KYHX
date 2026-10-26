# Build the device collection firmware into build/collect/E-Ink-collect.bin.
# Run after ESP-IDF export:
#   . 'F:\esp\v6.1\esp-idf\export.ps1'
#   pwsh -File tools/collect/build_collect_firmware.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out = Join-Path $root "build-collect"
New-Item -ItemType Directory -Force $out | Out-Null

# Keep the normal sdkconfig untouched; build a separate config with collect mode.
Copy-Item (Join-Path $root "sdkconfig") (Join-Path $out "sdkconfig") -Force
$cfg = Join-Path $out "sdkconfig"
$text = Get-Content $cfg -Raw
$text = $text -replace "# CONFIG_APP_MODE_COLLECT is not set", "CONFIG_APP_MODE_COLLECT=y"
if ($text -notmatch "CONFIG_APP_MODE_COLLECT=y") {
    $text = $text -replace "CONFIG_APP_BENCH=y", "CONFIG_APP_MODE_COLLECT=y`r`nCONFIG_APP_BENCH=y"
}
Set-Content $cfg $text -Encoding utf8

Push-Location $root
try {
    idf.py -B build-collect -D SDKCONFIG="$cfg" build
    New-Item -ItemType Directory -Force (Join-Path $root "build/collect") | Out-Null
    python -m esptool --chip esp32s3 merge-bin --flash-mode dio --flash-freq 80m --flash-size 16MB `
        -o build/collect/E-Ink-collect.bin `
        0x0 build-collect/bootloader/bootloader.bin `
        0x8000 build-collect/partition_table/partition-table.bin `
        0x10000 build-collect/E-Ink-English-Learning-Device-KYHX.bin
} finally {
    Pop-Location
}
