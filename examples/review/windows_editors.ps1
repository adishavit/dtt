# Run from Windows PowerShell, or powershell.exe -File through WSL interop.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$stage = Join-Path $root 'build/native-vs'
New-Item -ItemType Directory -Force $stage | Out-Null
Get-ChildItem (Join-Path $root 'ide-plugins/vs-dtt') -File |
    Where-Object { $_.Extension -in '.cs', '.csproj', '.vsct', '.vsixmanifest' } |
    Copy-Item -Destination $stage -Force
& dotnet build (Join-Path $stage 'DTT.csproj') --nologo -p:NuGetAudit=false
if ($LASTEXITCODE -ne 0) { throw 'Native Visual Studio extension build failed' }

$fixture = Join-Path $root ('build/native archive ' + [guid]::NewGuid())
try {
    New-Item -ItemType Directory -Force (Join-Path $fixture 'llvm-package/bin'), (Join-Path $fixture 'extracted') | Out-Null
    Set-Content (Join-Path $fixture 'llvm-package/bin/clang++.exe') 'archive layout probe'
    & tar -cJf (Join-Path $fixture 'llvm.tar.xz') -C $fixture llvm-package
    if ($LASTEXITCODE -ne 0) { throw 'Fixture archive creation failed' }
    & tar -xf (Join-Path $fixture 'llvm.tar.xz') -C (Join-Path $fixture 'extracted') --strip-components=1 --no-same-owner
    if ($LASTEXITCODE -ne 0) { throw 'Native archive extraction failed' }
    if ((Get-Content (Join-Path $fixture 'extracted/bin/clang++.exe')) -ne 'archive layout probe') { throw 'Wrong extraction layout' }
    Write-Host 'Native Windows build and archive extraction passed.'
} finally { Remove-Item $fixture -Recurse -Force }
