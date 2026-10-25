# Install the Python 3.12 training / conversion toolchain.
#
# Why a short path: TensorFlow wheels contain very deep include paths and fail
# on Windows unless LongPathsEnabled is on.  We keep the real venv at
# C:\venv-eink and expose it in the repo as .venv-train (junction), so every
# project script can keep using .venv-train\Scripts\python.exe.
#
# Usage (from repo root):  pwsh -File tools/setup/install-train.ps1
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$venv = if ($env:EINK_VENV) { $env:EINK_VENV } else { 'C:\venv-eink' }
$link = Join-Path $root '.venv-train'
$py = Join-Path $venv 'Scripts\python.exe'
$req = Join-Path $root 'tools\train\requirements-convert.txt'
$lock = Join-Path $root 'tools\train\requirements.lock.txt'

if (-not (Test-Path $py)) {
    Write-Host "Creating venv at $venv with Python 3.12 ..."
    py -3.12 -m venv $venv
}

if (-not (Test-Path $link)) {
    Write-Host "Creating junction $link -> $venv ..."
    New-Item -ItemType Junction -Path $link -Target $venv | Out-Null
}

Write-Host "== upgrade pip =="
& $py -m pip install --upgrade pip setuptools wheel
if ($LASTEXITCODE -ne 0) { throw "pip upgrade failed" }

Write-Host "== install PyTorch CPU =="
& $py -m pip install "torch==2.14.1+cpu" --index-url https://download.pytorch.org/whl/cpu
if ($LASTEXITCODE -ne 0) { throw "PyTorch install failed" }

Write-Host "== install core + TensorFlow/onnx2tf =="
& $py -m pip install -r $req
if ($LASTEXITCODE -ne 0) { throw "requirements install failed" }

Write-Host "== freeze =="
& $py -m pip freeze | Set-Content -Encoding UTF8 $lock
Write-Host "Wrote $lock"
& $py -c "import torch, tensorflow as tf, onnx, onnxruntime, librosa, soundfile; print('torch', torch.__version__); print('tf', tf.__version__); print('onnx', onnx.__version__); print('onnxruntime', onnxruntime.__version__); print('librosa', librosa.__version__); print('soundfile', soundfile.__version__)"
if ($LASTEXITCODE -ne 0) { throw "import check failed" }
