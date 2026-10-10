# Distribution and platform validation

Updated: 2026-10-10. DTT is built against LLVM/Clang 18. A plugin requires a
compatible Clang host; bundling Z3 does not make LLVM plugin ABIs interchangeable.

## Linux runtime archive

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++-18 -DDTT_BUNDLE_Z3=ON
cmake --build build -j8
cmake --install build --prefix "$PWD/build/package"
tar -czf build/dtt-linux-x86_64.tar.gz -C build/package .
```

The archive contains the plugin and the resolved Z3 shared library, preserving
its SONAME and symlinks under `lib/`. The installed plugin uses `$ORIGIN` to find
Z3 beside itself. Built-in contracts are embedded; their editable sources and
optional external contracts are installed under `share/dtt/annotations`.
Z3 notices are included under `share/dtt/licenses/z3`.

Keep the extracted directory together and run:

```sh
clang++-18 -fplugin=./lib/libDTTPlugin.so -std=c++20 -fsyntax-only your_code.cpp
```

The matching Clang host and OS/C++ runtimes remain prerequisites. The supplied
release build targets Ubuntu 24.04 x86-64; an archive built there is not a promise
of compatibility with older glibc systems. Custom Z3 builds may have additional
runtime dependencies (for example GMP); validate those before distribution.

`DTT_BUNDLE_Z3` defaults to `OFF` for source installs that use system Z3. It is
currently supported only on Linux. The release packaging definition enables it
and adds `dtt-linux-x86_64.tar.gz`. The legacy bare `.so` asset used by existing
editor installers still requires system Z3; installers have not been migrated
to the archive. No release was published during this validation.

## Validation evidence

| Platform | Checked here | Still required |
|----------|--------------|----------------|
| Linux / Clang 18 | Hosted Linux CI at `1e2762c`; local libstdc++ and libc++ tests; archive extraction and relocation into a path containing spaces; isolated safe, unsafe, STL and solver probes with host Z3 and the repository hidden; missing bundled Z3 correctly fails loading | Validate each published artifact on its advertised OS baseline |
| Windows | Native Visual Studio extension build with Windows .NET via WSL interop; native tar extraction with spaces in paths; MSVC Build Tools and CMake available | Matching Clang/LLVM development toolchain, native analyzer build/load, Z3 DLL deployment, IDE installation and suite execution |
| macOS | Source/CMake inspection; Linux libc++ tests are not native macOS validation | A macOS host with Homebrew LLVM 18/Z3; native build, installation, plugin load and tests |

Cross-platform source inspection does not count as a native platform test.
Hosted CI build/test results also do not establish release-package validation.
Windows and macOS packaging are not declared validated.

## Reproduce locally

```sh
python3 examples/review/runtime_package.py --build build --clang /usr/bin/clang++-18
# Optional stronger Linux isolation check (requires bubblewrap):
python3 examples/review/runtime_package.py --build build --clang /usr/bin/clang++-18 --isolated
ctest --test-dir build -R '^review_' --output-on-failure
```

Run full suites in the background as described in
[Implementation review status](plans/IMPLEMENTATION_REVIEW_STATUS.md).
The runtime-package check must use a build configured with `DTT_BUNDLE_Z3=ON`.

For macOS, use the Homebrew compiler explicitly (Apple Clang is not a substitute):

```sh
cmake -S . -B build \
  -DCMAKE_C_COMPILER="$(brew --prefix llvm@18)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm@18)/bin/clang++" \
  -DLLVM_DIR="$(brew --prefix llvm@18)/lib/cmake/llvm" \
  -DClang_DIR="$(brew --prefix llvm@18)/lib/cmake/clang" \
  -DCMAKE_PREFIX_PATH="$(brew --prefix z3)"
cmake --build build -j8
```

For Windows, use the pinned vcpkg LLVM/Clang toolchain and explicit `CLANGXX`
configuration in `.github/workflows/release.yml` as a starting point. The pin now
selects LLVM 18.1.6 with `export-symbols`; the previous pin selected LLVM 17.0.2.
The installed native Windows build tools are usable through WSL interop, but the
LLVM/Clang development libraries and matching driver are not installed. Building
that toolchain is still required before native analyzer validation. A bare
`DTTPlugin.dll` still needs its matching Z3 DLL discoverable by the loader.
The workflow definition itself is not evidence of a successful Windows build.

To provision the native dependencies locally from Windows PowerShell:

```powershell
.\examples\review\windows-toolchain\build.ps1
```

This uses its own pinned vcpkg checkout under `C:\dev\dtt-native`, builds only
Release libraries for x64, enables LLVM plugin exports and limits compilation
to four jobs. `-BuildRoot` and `-Jobs` override those defaults. Visual Studio
2022 C++ Build Tools and its ATL component are required; vcpkg's LLVM port checks
for `atlbase.h` before building. Installing ATL may require Windows administrator
approval. Existing shared vcpkg installations are not modified.

Toolchain provisioning is a prerequisite, not evidence that DTT itself builds or
loads on Windows. Native plugin and test-suite validation remain outstanding.
After provisioning, run `.\examples\review\windows-toolchain\validate.ps1`.
It configures DTT in the private build directory, checks safe and unsafe plugin
probes, then runs the full suite with known xfail cases reported separately.
Logs and the JUnit report are retained under `C:\dev\dtt-native\dtt`.
The optional `-WaitForProcessId` argument queues this validation behind an
already-running native provisioning process without polling it.
