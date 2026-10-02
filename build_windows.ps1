param(
    [string]$BuildDir = "build-windows",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = [System.IO.Path]::GetFullPath((Join-Path $root $BuildDir))
$configure = @("-S", (Join-Path $root "reimpl"), "-B", $build)

if ($IsWindows -or $env:OS -eq "Windows_NT") {
    $configure += @("-G", "Visual Studio 17 2022", "-A", "x64")
}
if ($VcpkgRoot) {
    $toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    if (-not (Test-Path $toolchain)) {
        throw "vcpkg toolchain not found: $toolchain"
    }
    $configure += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
}
& cmake @configure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $build --config $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& ctest --test-dir $build -C $Configuration --output-on-failure
exit $LASTEXITCODE
