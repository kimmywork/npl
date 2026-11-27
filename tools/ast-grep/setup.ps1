# Build the tree-sitter-scheme shared library for ast-grep on Windows.
# Usage: .\tools\ast-grep\setup.ps1
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$CacheDir = Join-Path $Root ".cache\ast-grep"
$RepoDir = Join-Path $CacheDir "tree-sitter-scheme"
$OutDll = Join-Path $CacheDir "scheme-windows-x86_64.dll"
$Rev = if ($env:TREE_SITTER_SCHEME_REV) { $env:TREE_SITTER_SCHEME_REV } else { "45c72463ac4260ef6e07d1c0642ed43d4e9957b4" }

New-Item -ItemType Directory -Force $CacheDir | Out-Null

if (-not (Test-Path (Join-Path $RepoDir ".git"))) {
    Remove-Item -Recurse -Force $RepoDir -ErrorAction SilentlyContinue
    git init -q $RepoDir
    git -C $RepoDir remote add origin https://github.com/6cdh/tree-sitter-scheme
    git -C $RepoDir fetch --depth 1 origin $Rev
    git -C $RepoDir checkout -q FETCH_HEAD
}

$clang = Get-Command clang -ErrorAction SilentlyContinue
if (-not $clang -and (Test-Path "D:\msys64\clang64\bin\clang.exe")) {
    $env:PATH = "D:\msys64\clang64\bin;$env:PATH"
    $clang = Get-Command clang
}
if (-not $clang) {
    throw "clang not found in PATH (and D:\msys64\clang64\bin\clang.exe missing)"
}

& $clang.Source -shared -O2 `
    -I (Join-Path $RepoDir "src") `
    (Join-Path $RepoDir "src\parser.c") `
    "-Wl,--export-all-symbols" `
    -o $OutDll

Write-Host "Built $OutDll"
