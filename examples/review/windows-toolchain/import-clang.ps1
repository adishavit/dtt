# Reconstruct the host import library omitted by LLVM 18's Windows install rules.
# Supports x64 LLVM 18 built with LLVM_EXPORT_SYMBOLS_FOR_PLUGINS=ON. Its export
# list contains code imports; this is not a general-purpose PE import generator.
param(
    [Parameter(Mandatory = $true)][string]$Clang,
    [Parameter(Mandatory = $true)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$Clang = (Resolve-Path $Clang).Path
if ([IO.Path]::GetFileName($Clang) -ine 'clang.exe') {
    throw 'The plugin host must be named clang.exe'
}
$tools = Split-Path $Clang
$readobj = Join-Path $tools 'llvm-readobj.exe'
$dlltool = Join-Path $tools 'llvm-dlltool.exe'
foreach ($tool in @($readobj, $dlltool)) {
    if (-not (Test-Path $tool -PathType Leaf)) { throw "Required LLVM tool missing: $tool" }
}

$version = & $Clang --version
if ($LASTEXITCODE -ne 0) { throw 'Could not read the Clang version' }
if (-not ($version -match '^clang version 18\.')) { throw 'This helper requires LLVM 18' }

$dump = & $readobj --coff-exports $Clang
if ($LASTEXITCODE -ne 0) { throw 'Could not read the Clang export table' }
if (-not ($dump -match '^Arch: x86_64$')) { throw 'This helper requires x64 Clang' }
if ($dump -match '^  ForwardedTo:') { throw 'Forwarded exports are not supported' }
$exports = @($dump | ForEach-Object {
    if ($_ -match '^  Name: (.+)$') { $Matches[1] }
})
$entries = @($dump | Where-Object { $_ -eq 'Export {' })
if ($exports.Count -ne $entries.Count) { throw 'Unnamed or malformed exports are not supported' }
if (-not ($exports -match '^\?add_node@\?\$Registry@VPluginASTAction@clang@@')) {
    throw 'Clang does not export its plugin registry; build with LLVM_EXPORT_SYMBOLS_FOR_PLUGINS=ON'
}

$OutputPath = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force (Split-Path $OutputPath) | Out-Null
$definition = [IO.Path]::ChangeExtension($OutputPath, '.def')
@('EXPORTS') + $exports | Set-Content $definition -Encoding ASCII
& $dlltool -m i386:x86-64 -D clang.exe -d $definition -l $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'Could not generate the Clang import library' }
Write-Host "Created $OutputPath from $($exports.Count) exports in $Clang"
