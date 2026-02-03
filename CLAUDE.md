# DTT Project Instructions

## Project Overview

**DTT (Dependent Type Tracking)** is a Clang compiler plugin that catches CVE-class bugs at compile time by tracking semantic properties through C++ code. It detects:

- Buffer overflows and out-of-bounds access
- Null pointer dereferences
- Use-after-free and double-free
- Use-after-move
- Empty container access (Heartbleed-class bugs)

The plugin proves code safe or shows exact counterexamples without runtime overhead.

## Architecture

### Directory Structure

```
dtt/
├── src/                        # Plugin implementation
│   ├── DTTPlugin.cpp           # Main plugin (~10k lines) - AST visitor, proof system
│   ├── DTTStdLib.cpp           # STL type annotations (vector, optional, etc.)
│   ├── DTTStdLib.h             # STL annotation interface
│   ├── DTTCLib.cpp             # C library annotations (FILE*, strcpy, etc.)
│   ├── DTTCLib.h               # C library annotation interface
│   └── DTTTypes.h              # Shared type definitions
├── include/
│   └── dtt.h                   # Public API header for user annotations
├── examples/                   # Test suite (naming convention enforced)
│   ├── *_pass.cpp              # Should compile without DTT errors
│   ├── *_fail.cpp              # Should produce DTT errors (WILL_FAIL)
│   ├── *_xfail.cpp             # Known gaps - DTT should catch but doesn't yet
│   └── *_silent.cpp            # Out-of-scope cases, intentionally not tracked
├── docs/                       # Design documentation and specs
│   ├── plans/                  # Future features and proposals
│   └── archive/                # Historical design decisions
├── .github/workflows/ci.yml    # CI configuration (Linux daily, macOS monthly)
├── CMakeLists.txt              # Build configuration
├── README.md                   # Quick start guide
├── GUIDE.md                    # Full feature reference
├── TDD.md                      # Test-driven development workflow
└── DEBUGGING_NOTES.md          # Platform-specific debugging tips
```

### Source Files

| File | Purpose | Size |
|------|---------|------|
| `DTTPlugin.cpp` | Main visitor, proof system, constraint parsing, Z3 integration | ~10k lines |
| `DTTStdLib.cpp` | Hardcoded annotations for STL types (vector, optional, etc.) | ~400 lines |
| `DTTCLib.cpp` | Annotations for C library functions (FILE*, strcpy, etc.) | ~200 lines |
| `DTTTypes.h` | Shared data structures (TrackingState, FieldPath, Proof) | ~350 lines |

### Key Data Structures (in DTTPlugin.cpp)

- **TrackingState**: Known, UnknownLost (Category C), UnknownSolver (Category D), Untracked
- **PointerLifetime**: Valid, Freed, Unknown (for use-after-free detection)
- **FieldPath**: Represents paths like `obj.v._size` or `arg0->buffer._capacity`
- **Proof**: Tracks constraints (GT, GE, LT, LE, EQ, NE) with Z3 SMT solver support

### Diagnostic Categories

| Category | Meaning | Severity |
|----------|---------|----------|
| **A** | Untracked | N/A |
| **B** | Never tracked | N/A |
| **C** | Lost tracking | Warning (inform) / Error (enforce) |
| **D** | Solver failed | Warning (inform) / Error (enforce) |
| **E** | Safe (proven) | N/A |
| **F** | Proven unsafe | Always Error |
| **G** | Annotation error | Always Error |

---

## Documentation Rules

- **NEVER write the number of tests in docs** - test counts change frequently and become stale

---

## Build & Test

```bash
# Configure
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18

# Build
cmake --build build

# Run ALL tests locally
ctest --test-dir build --output-on-failure

# Run specific test
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/01_basic_pass.cpp

# Run tests by pattern
ctest --test-dir build -R "nullptr"      # Tests matching "nullptr"
ctest --test-dir build -L "xfail"        # Only xfail tests
ctest --test-dir build -LE "xfail"       # Exclude xfail tests
```

### Dependencies

| Platform | Packages |
|----------|----------|
| **Ubuntu/Debian** | `clang-18 llvm-18-dev libclang-18-dev cmake libz3-dev` |
| **macOS** | `brew install llvm@18 cmake z3` |

### macOS Build

```bash
cmake -B build \
  -DCMAKE_C_COMPILER=$(brew --prefix llvm@18)/bin/clang \
  -DCMAKE_CXX_COMPILER=$(brew --prefix llvm@18)/bin/clang++ \
  -DLLVM_DIR=$(brew --prefix llvm@18)/lib/cmake/llvm \
  -DClang_DIR=$(brew --prefix llvm@18)/lib/cmake/clang
```

---

## Test Naming Convention

| Suffix | Purpose | CMake Behavior |
|--------|---------|----------------|
| `*_pass.cpp` | Should compile without DTT errors | Normal test |
| `*_fail.cpp` | Should produce DTT errors | `WILL_FAIL TRUE`, enforce mode |
| `*_xfail.cpp` | Known gaps - DTT should catch but doesn't | `WILL_FAIL TRUE`, labeled "xfail" |
| `*_xpass.cpp` | Known false positives - safe code DTT incorrectly rejects | `WILL_FAIL TRUE`, labeled "xpass;false_positive" |
| `*_silent.cpp` | Out-of-scope cases | No errors expected |

### xfail Test Semantics

xfail tests document **false negatives** - unsafe code that DTT should catch but doesn't yet.

- Uses `WILL_FAIL TRUE` in CMake
- Gap exists → compilation succeeds → test "fails" (expected)
- Gap fixed → compilation fails (DTT catches it) → test passes (update to `*_fail.cpp`)

**Local runs:** Always run ALL tests including xfail to see current gap status.

**CI:** Runs xfail tests separately with `continue-on-error: true` so gaps don't block CI.

### xpass Test Semantics

xpass tests document **false positives** - safe code that DTT incorrectly rejects.

- Uses `WILL_FAIL TRUE` in CMake (mirror of xfail)
- False positive exists → DTT error → compilation fails → `WILL_FAIL` satisfied → test passes
- False positive fixed → no error → compilation succeeds → `WILL_FAIL` violated → test fails (rename to `*_pass.cpp`)

---

## Bug Fix Workflow (TDD - Non-negotiable)

When fixing bugs or addressing code review findings:

1. **Write test FIRST** that demonstrates the bug
2. **Run test** - confirm it FAILS (proves bug exists)
3. **Implement fix**
4. **Run test** - confirm it PASSES

No exceptions. Test first, fix second.

### MANDATORY TDD Rules

1. **Tests go in the repo** - Write all debugging/test files in `examples/` folder, NEVER in `/tmp`
2. **Multiple variants per gap** - ALWAYS create multiple test variants for each detected gap, ALL in the same test file (e.g., `509_feature_pass.cpp` with variant1, variant2, etc.)
3. **Fix uncovered xfail tests** - When your fix causes an xfail test to pass (DTT now catches the issue), rename it from `*_xfail.cpp` to `*_fail.cpp` and update CMakeLists.txt
3b. **Fix uncovered xpass tests** - When your fix causes an xpass test to pass (false positive eliminated), rename it from `*_xpass.cpp` to `*_pass.cpp`
4. **MUST run test before implementing** - Confirm test FAILS first (proves the gap/bug exists)
5. **MUST run test after implementing** - Confirm test PASSES (proves the fix works)
6. **Never skip verification steps** - Both pre and post test runs are mandatory, not optional

### TDD Mantra

See **@TDD.md** - recite before every bug fix.

---

## Plugin Usage

```bash
# Default mode (inform) - warnings for coverage gaps
clang++ -fplugin=./build/libDTTPlugin.so -std=c++20 file.cpp

# Strict mode - errors for any uncertainty
clang++ -fplugin=./build/libDTTPlugin.so -fplugin-arg-dtt---dtt-mode=enforce -std=c++20 file.cpp

# Quiet mode - only error on proven bugs
clang++ -fplugin=./build/libDTTPlugin.so -fplugin-arg-dtt---dtt-mode=quiet -std=c++20 file.cpp
```

---

## Key Implementation Patterns

### Namespace Traversal (for libc++ compatibility)

When checking if something is in a specific namespace, always traverse the full parent chain. libc++ uses inline namespaces like `std::__1::move`:

```cpp
DeclContext *DC = Decl->getDeclContext();
while (DC) {
    if (auto *NS = dyn_cast<NamespaceDecl>(DC)) {
        if (NS->getNameAsString() == "std") {
            return true;
        }
    }
    DC = DC->getParent();
}
```

### Adding Diagnostic Output for Debugging

```cpp
auto &DE = Ctx.getDiagnostics();
unsigned ID = DE.getCustomDiagID(DiagnosticsEngine::Warning,
    "DEBUG: lhsPath='%0' rhsPath='%1'");
DE.Report(E->getBeginLoc(), ID) << lhsPath << rhsPath;
```

### STL Method Annotations (DTTStdLib.cpp)

```cpp
// Pattern for adding new method annotations:
MethodAnnotations["std::vector"]["front"][0] = {
    .preconditions = {"_size > 0"},
};

MethodAnnotations["std::vector"]["push_back"][1] = {
    .postconditions = {"_size == old(_size) + 1"},
};
```

### C Library Function Annotations (DTTCLib.cpp)

```cpp
// Pattern for tracking C functions:
CLibFunctions["strlen"] = {
    .checkPointerArgs = {0},  // Check arg0 for use-after-free
};

CLibFunctions["fread"] = {
    .preconditions = {"arg3._nonnull == 1"},  // FILE* must be non-null
};
```

---

## CI/CD Configuration

**Linux CI** (daily @ 00:00 UTC):
- Ubuntu 24.04, clang-18
- Runs all pass/fail/silent tests
- xfail tests run with `continue-on-error: true`

**macOS CI** (monthly 1st @ 01:00 UTC):
- macOS 14, Homebrew LLVM@18
- Catches libc++ incompatibilities
- 10x cost, so runs monthly

---

## xpass Test Semantics

xpass tests document **false positives** - safe code that DTT incorrectly rejects.

- Uses `WILL_FAIL TRUE` in CMake (mirror of xfail)
- False positive exists → DTT error → compilation fails → `WILL_FAIL` satisfied → test passes
- False positive fixed → no error → compilation succeeds → `WILL_FAIL` violated → test fails (rename to `*_pass.cpp`)

## Debugging Reference

See **@DEBUGGING_NOTES.md** for:
- macOS/libc++ quirks and AST differences
- CI debugging workflow
- Diagnostic output patterns

---

## LLVM/Clang Coding Style

**READ BEFORE WRITING CODE:** See **@docs/LLVM_CODING_PRINCIPLES.md**

Critical rules:
- **Never use `std::string`** in hot paths - use `StringRef` or `SmallString<N>`
- **Never use `std::vector`** - use `SmallVector<T, N>`
- **`getName()` requires `getIdentifier()` check** - it crashes on operators
- **Never use `getNameAsString()`** when `getName()` would work (it allocates)

---

## Related Documentation

| File | Purpose |
|------|---------|
| `README.md` | Quick start and feature overview |
| `GUIDE.md` | Comprehensive feature reference |
| `TDD.md` | Test-driven development mantra |
| `DEBUGGING_NOTES.md` | Platform debugging tips |
| `docs/LLVM_CODING_PRINCIPLES.md` | LLVM/Clang coding style guide |
| `docs/VALUE_CONCEPTS_SPEC.md` | Bounded quantifiers and collection properties |
| `docs/SUM_TYPE_TRACKING.md` | std::variant and std::any tracking |
| `docs/RESOURCE_TRACKING_DESIGN.md` | Automatic resource leak detection |
| `docs/HEARTBLEED_AUTOTRACK_SPEC.md` | Buffer over-read detection spec |
| `docs/plans/` | Future feature proposals |
