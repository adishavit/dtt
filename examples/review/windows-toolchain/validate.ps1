# Build and exercise DTT with the private toolchain created by build.ps1.
param(
    [string]$BuildRoot = 'C:\dev\dtt-native',
    [int]$WaitForProcessId = 0
)
$ErrorActionPreference = 'Stop'
if ($WaitForProcessId -gt 0 -and (Get-Process -Id $WaitForProcessId -ErrorAction SilentlyContinue)) {
    Write-Host "Waiting for toolchain process $WaitForProcessId"
    Wait-Process -Id $WaitForProcessId
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$prefix = Join-Path $BuildRoot 'installed/x64-windows-dtt'
$clang = Join-Path $prefix 'tools/llvm/clang++.exe'
$clangcl = Join-Path $prefix 'tools/llvm/clang-cl.exe'
foreach ($file in @($clang, $clangcl, (Join-Path $prefix 'include/z3++.h'))) {
    if (-not (Test-Path $file)) { throw "Toolchain is incomplete: $file" }
}
$python = Get-ChildItem (Join-Path $BuildRoot 'downloads/tools') -Filter python.exe -Recurse |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $python) { throw 'The toolchain Python executable was not found' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'Visual Studio C++ Build Tools not found' }
$build = Join-Path $BuildRoot 'dtt'
New-Item -ItemType Directory -Force $build | Out-Null
$report = Join-Path $build 'full-suite.xml'
if (Test-Path $report) { Remove-Item $report }
$batch = Join-Path $BuildRoot 'validate-dtt.cmd'
@"
@echo off
call "$vs\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64
if errorlevel 1 exit /b 1
set "PATH=$prefix\bin;$prefix\tools\llvm;%PATH%"
cmake -S "$repo" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$clangcl" -DCMAKE_CXX_COMPILER="$clangcl" -DCLANGXX="$clang" -DCMAKE_TOOLCHAIN_FILE="$BuildRoot\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-dtt -DVCPKG_OVERLAY_TRIPLETS="$PSScriptRoot\triplets" -DVCPKG_INSTALLED_DIR="$BuildRoot\installed" -DVCPKG_MANIFEST_MODE=OFF -DLLVM_DIR="$prefix\share\llvm" -DClang_DIR="$prefix\share\clang" -DPython3_EXECUTABLE="$python"
if errorlevel 1 exit /b 1
cmake --build "$build" --parallel 4
if errorlevel 1 exit /b 1
"$clang" -fplugin="$build\DTTPlugin.dll" -std=c++20 -fsyntax-only "$repo\examples\01_basic_pass.cpp" > "$build\probe-safe.log" 2>&1
if errorlevel 1 exit /b 1
"$clang" -fplugin="$build\DTTPlugin.dll" -std=c++20 -fsyntax-only "$repo\examples\02_basic_fail.cpp" > "$build\probe-unsafe.log" 2>&1
if not "%errorlevel%"=="1" exit /b 1
findstr /C:"error: DTT:" "$build\probe-unsafe.log" >nul
if errorlevel 1 exit /b 1
ctest --test-dir "$build" -j4 --output-on-failure --output-junit "$report" > "$build\full-suite.log" 2>&1
exit /b %errorlevel%
"@ | Set-Content $batch -Encoding ASCII
& $batch
$result = $LASTEXITCODE
if (-not (Test-Path $report)) {
    throw "Native build or compile probe failed (exit $result); inspect $build and this process log"
}
[xml]$suite = Get-Content $report -Raw
$cases = @($suite.SelectNodes('//testcase'))
if ($cases.Count -eq 0) { throw 'Native test report contains no test cases' }
$unexpected = @($cases | Where-Object {
    ($_.SelectSingleNode('failure') -or $_.SelectSingleNode('error')) -and $_.GetAttribute('name') -notmatch '_xfail($|_)'
})
if ($unexpected.Count -gt 0) {
    $unexpected | ForEach-Object { Write-Host "Unexpected failure: $($_.GetAttribute('name'))" }
    throw "Native regression failures: see $build\full-suite.log"
}
if ($result -ne 0 -and $result -ne 8) { throw "CTest failed with exit $result" }
Write-Host "Native build, plugin probes and regression suite passed apart from documented xfail cases. Logs: $build"
