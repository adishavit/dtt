# Provision a private, Release-only LLVM/Z3 toolchain for native DTT validation.
param(
    [string]$BuildRoot = 'C:\dev\dtt-native',
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
if ($Jobs -lt 1) { throw 'Jobs must be positive' }
$manifest = Get-Content (Join-Path $PSScriptRoot 'vcpkg.json') -Raw | ConvertFrom-Json
$revision = $manifest.'builtin-baseline'
$vcpkgRoot = Join-Path $BuildRoot 'vcpkg'
New-Item -ItemType Directory -Force $BuildRoot | Out-Null
if (-not (Test-Path (Join-Path $vcpkgRoot '.git'))) {
    & git init $vcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg git init failed' }
    & git -C $vcpkgRoot remote add origin https://github.com/microsoft/vcpkg.git
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg remote setup failed' }
    & git -C $vcpkgRoot fetch --depth=1 origin $revision
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg fetch failed' }
    & git -C $vcpkgRoot checkout --detach $revision
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg checkout failed' }
}
$actualRevision = & git -C $vcpkgRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $revision) {
    throw "Existing private vcpkg checkout does not match $revision"
}
if (-not (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
    & (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed' }
}
$env:VCPKG_MAX_CONCURRENCY = "$Jobs"
$env:VCPKG_DISABLE_METRICS = '1'
& (Join-Path $vcpkgRoot 'vcpkg.exe') install `
    "--x-manifest-root=$PSScriptRoot" `
    "--overlay-triplets=$(Join-Path $PSScriptRoot 'triplets')" `
    '--triplet=x64-windows-dtt' `
    '--host-triplet=x64-windows-dtt' `
    "--x-install-root=$(Join-Path $BuildRoot 'installed')" `
    "--x-buildtrees-root=$(Join-Path $BuildRoot 'buildtrees')" `
    "--x-packages-root=$(Join-Path $BuildRoot 'packages')" `
    "--downloads-root=$(Join-Path $BuildRoot 'downloads')"
if ($LASTEXITCODE -ne 0) { throw 'Native LLVM/Z3 dependency build failed' }
Write-Host "Native toolchain installed under $BuildRoot\installed\x64-windows-dtt"
