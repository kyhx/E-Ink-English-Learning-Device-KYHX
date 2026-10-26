# Start the local OpenPronounce web app (http://127.0.0.1:8000).
# Prerequisites: third_party/OpenPronounce cloned, deps installed in C:\venv-eink.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$app = Join-Path $root "third_party\OpenPronounce"
if (-not (Test-Path $app)) { throw "missing $app; git clone https://github.com/Halleck45/OpenPronounce.git third_party/OpenPronounce" }

$env:HF_HOME = Join-Path $root "build\openpronounce\hf"
$env:PATH = "C:\Program Files\eSpeak NG;" + $env:PATH
$env:PYTHONIOENCODING = "utf-8"
New-Item -ItemType Directory -Force $env:HF_HOME | Out-Null

Set-Location $app
& "C:\venv-eink\Scripts\python.exe" -m uvicorn server:app --host 127.0.0.1 --port 8000
