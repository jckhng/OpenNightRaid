[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = "Stop"

$sourceExecutable = (Resolve-Path -LiteralPath $Executable).Path
$sourceDirectory = Split-Path -Parent $sourceExecutable
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("niteraid-startup-assets-" + [Guid]::NewGuid().ToString("N"))
$testExecutable = Join-Path $testRoot (Split-Path -Leaf $sourceExecutable)
$runtimeDll = Join-Path $sourceDirectory "SDL2.dll"

New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    Copy-Item -LiteralPath $sourceExecutable -Destination $testExecutable
    if (Test-Path -LiteralPath $runtimeDll) {
        Copy-Item -LiteralPath $runtimeDll -Destination (Join-Path $testRoot "SDL2.dll")
    }

    Push-Location -LiteralPath $testRoot
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $missingOutput = & $testExecutable 2>&1
    $missingExitCode = $LASTEXITCODE
    if ($missingExitCode -eq 0) {
        throw "interactive startup unexpectedly succeeded without an asset archive or bundle: $($missingOutput -join "`n")"
    }
    if (-not (($missingOutput -join "`n") -match "supported GRAPHICS\.NTR or GRAPHICS\.NRD archive")) {
        throw "missing-asset startup error was not precise: $($missingOutput -join "`n")"
    }

    New-Item -ItemType Directory -Path (Join-Path $testRoot "assets") | Out-Null
    Set-Content -LiteralPath (Join-Path $testRoot "assets/import_manifest.json") -Value "{}"
    $incompleteOutput = & $testExecutable 2>&1
    if ($LASTEXITCODE -eq 0) {
        throw "interactive startup accepted an incomplete legacy asset bundle: $($incompleteOutput -join "`n")"
    }
    if (-not (($incompleteOutput -join "`n") -match "complete legacy development asset bundle")) {
        throw "incomplete-bundle startup error was not precise: $($incompleteOutput -join "`n")"
    }

    $headlessOutput = & $testExecutable --headless --max-frames=1 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "headless startup regressed without original assets: $($headlessOutput -join "`n")"
    }
    $ErrorActionPreference = $previousErrorActionPreference

    Pop-Location

    Write-Host "Startup asset requirement: PASS"
}
finally {
    if ((Get-Location).Path -eq $testRoot) {
        Pop-Location
    }
    Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
}
