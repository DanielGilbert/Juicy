# SPDX-License-Identifier: GPL-3.0-only
# Build Juicy and install it on the connected FM-1, on Windows (see README.md).
#   .\build_install.ps1
# Needs Docker Desktop and Python 3. The first run builds the Docker image juicy-build and a Python venv (.venv) with
# mido and python-rtmidi. The installer asks before it writes.

function Invoke-Checked([string]$what, [scriptblock]$command) {
    & $command
    if ($LASTEXITCODE -ne 0) { throw "build_install: $what failed (exit code $LASTEXITCODE)" }
}

Push-Location $PSScriptRoot
try {
    docker info *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'Starting Docker Desktop ...'
        docker desktop start *> $null
        $waitedSeconds = 0
        do {
            Start-Sleep -Seconds 2
            $waitedSeconds += 2
            docker info *> $null
        } while ($LASTEXITCODE -ne 0 -and $waitedSeconds -lt 120)
        if ($LASTEXITCODE -ne 0) { throw 'build_install: Docker did not start (is Docker Desktop installed?)' }
    }

    docker image inspect juicy-build *> $null
    if ($LASTEXITCODE -ne 0) {
        Invoke-Checked 'building the Docker image' { docker build --platform linux/amd64 -t juicy-build -f tools/Dockerfile tools }
    }
    Invoke-Checked 'the build' { docker run --rm -v "${PSScriptRoot}:/work" juicy-build sh build.sh }

    $python = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
    if (-not (Test-Path $python)) {
        Invoke-Checked 'creating .venv' { python -m venv .venv }
        Invoke-Checked 'installing mido and python-rtmidi' { & $python -m pip install --quiet mido python-rtmidi }
    }
    & $python tools/fm1_install.py build/juicy.fwsc   # (it says itself what went wrong, or "cancelled")
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
