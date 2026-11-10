param(
    [ValidateSet("x64", "ARM64")]
    [string]$Architecture = "x64",
    [switch]$Run,
    [string]$Model
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build/windows-$Architecture"
$dist = Join-Path $root "dist/windows-$Architecture"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "Install CMake 3.24 or later and make it available on PATH."
}

function Invoke-CMake {
    & cmake @args
    if ($LASTEXITCODE -ne 0) { throw "CMake exited with code $LASTEXITCODE." }
}

Invoke-CMake -S $root -B $build -G "Visual Studio 17 2022" -A $Architecture
Invoke-CMake --build $build --config Release --parallel
Invoke-CMake --install $build --config Release --prefix $dist
$exe = Join-Path $dist "ModelViewer.exe"
Write-Host "Built $exe"

if ($Run) {
    if ($Model) {
        $path = (Resolve-Path -LiteralPath $Model).Path
        Start-Process -FilePath $exe -ArgumentList ('"{0}"' -f $path)
    } else {
        Start-Process -FilePath $exe
    }
}
