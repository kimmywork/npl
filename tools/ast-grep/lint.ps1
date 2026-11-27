# Run ast-grep rule unit tests and scan the repository (C + Scheme/NPL).
# Extra arguments (e.g. --format github) are forwarded to `ast-grep scan`.
# Usage: .\tools\ast-grep\lint.ps1
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Dll = Join-Path $Root ".cache\ast-grep\scheme-windows-x86_64.dll"
if (-not (Test-Path $Dll)) {
    & (Join-Path $PSScriptRoot "setup.ps1")
}

Push-Location $Root
try {
    Write-Host "==> Running ast-grep rule tests..."
    ast-grep test --custom-languages allow --skip-snapshot-tests
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "==> Scanning repository with ast-grep..."
    ast-grep scan --custom-languages allow @args runtime src lib
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "ast-grep lint: PASS"
} finally {
    Pop-Location
}
