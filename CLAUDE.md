# DTT Project Instructions

## Project Overview

**DTT (Dependent Type Tracking)** is a Clang compiler plugin that catches CVE-class bugs at compile time by tracking semantic properties through C++ code. It detects:

- Buffer overflows and out-of-bounds access
- Null pointer dereferences
- Use-after-free and double-free
- Use-after-move (via property invalidation on `std::move`)
- Empty container access (Heartbleed-class bugs)
- Resource leaks (FILE*, malloc, file descriptors, etc.)
- Type-unsafe variant/any access
- Uninitialized memory usage (scalars, pointers, POD struct members)
- Taint tracking: untrusted data (from `getenv`, `recv`, `scanf`, etc.) reaching dangerous sinks (`system`, `exec*`, format strings, allocation sizes)

What it actually does: tracks symbolic properties (`_size`, `_nonnull`, `@open`, etc.) through single-TU control flow, checks annotated preconditions against the proof store, and reports when a precondition is provably violated (Category F) or can't be proven (Categories C/D). Tracks taint from external sources through propagation chains and flags when tainted data reaches security-sensitive sinks. No runtime overhead.

What it doesn't do: whole-program analysis (single TU only, unless cross-TU serialization is used), aliasing/pointer analysis, concurrency, or dynamic dispatch. It doesn't "prove programs correct" — it proves specific tracked properties at specific call sites.

Key capabilities: ghost-field resource tracking, transitive precondition propagation through call chains (all properties via fixpoint, not just buffer sizes), backward search over annotation registry for fix suggestions, auto-inference of postconditions from method implementations, and taint tracking with 31 sources, 88 propagators, and 20 sinks (seeded from CSA GenericTaintChecker, cross-verified against LLVM 18).

## Architecture

### Directory Structure

```
dtt/
├── src/                        # Plugin implementation
│   ├── DTTPlugin.cpp           # Main plugin - AST visitor, proof system
│   ├── DTTAnnotationFile.cpp   # .dtt parser and built-in annotation loader
│   ├── DTTAnnotationFile.h     # Parser API, global annotation maps
│   └── DTTTypes.h              # Shared type definitions
├── annotations/                # Declarative annotation files (.dtt format)
│   ├── std.dtt                 # STL type annotations (vector, optional, etc.)
│   ├── libc.dtt                # C library annotations (FILE*, malloc, etc.)
│   ├── win32.dtt               # Windows API annotations (HANDLE, HeapAlloc, etc.)
│   └── test_custom.dtt         # Test fixture for --dtt-annotations= feature
├── include/
│   └── dtt.h                   # Public API header for user annotations
├── examples/                   # Test suite (naming convention enforced)
│   ├── *_pass.cpp              # Should compile without DTT errors
│   ├── *_fail.cpp              # Should produce DTT errors (WILL_FAIL)
│   ├── *_xfail.cpp             # Known gaps - DTT should catch but doesn't yet
│   ├── *_xpass.cpp             # Known false positives - safe code DTT rejects
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

| File | Purpose |
|------|---------|
| `DTTPlugin.cpp` | Main visitor, proof system, constraint parsing, Z3 integration |
| `DTTAnnotationFile.cpp` | `.dtt` file parser, built-in annotation loader, global maps |
| `DTTTypes.h` | Shared types (SymValue, ConstraintExpr, MethodKey, Postcondition) |
| `annotations/std.dtt` | STL type annotations (vector, optional, smart pointers, etc.) |
| `annotations/libc.dtt` | C library annotations (FILE*, malloc, fd, etc.) |
| `annotations/win32.dtt` | Windows API annotations (HANDLE, HeapAlloc, BSTR, etc.) |

### Key Data Structures (in DTTPlugin.cpp)

- **TrackingState**: Known, UnknownLost (Category C), UnknownSolver (Category D), Untracked
- **PointerLifetime**: Valid, Freed, Unknown (for use-after-free detection)
- **FieldPath**: Represents paths like `obj.v._size` or `arg0->buffer._capacity`
- **Proof**: Tracks constraints (GT, GE, LT, LE, EQ, NE) with Z3 SMT solver support

### Tracked Properties

| Property | Types | Purpose |
|----------|-------|---------|
| `_size` | vector, string, containers | Element count |
| `_has_value` | optional, any | Value presence |
| `_nonnull` | pointers, unique_ptr, shared_ptr | Non-null state |
| `_variant_index` | variant | Active alternative index |
| `_any_type` | any | Contained type identity |
| `@open`, `@allocated`, `@fd` | FILE*, malloc, file descriptors | Resource state (ghost) |
| `_tainted` | any value from external sources | Taint level (High/Medium/Low) |

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
| **H** | Memory safety (UAF, double-free, uninitialized, resource leak) | Always Error |

---

## Git Merge Policy

- **NEVER squash** when merging PRs or branches — preserve individual commits
- **NEVER fast-forward** — always use `--no-ff` to create a merge commit
- When merging via `gh pr merge`, use `--merge` (not `--squash`, not `--rebase`)
- The goal is a clear commit history with visible merge points

## Documentation Rules

- **NEVER write the number of tests in docs** - test counts change frequently and become stale
- **Honesty over theater** - describe what code *actually does*, not what it sounds like. If a feature is "a conditional branch in postcondition application," say that — don't call it "guard clauses from Prolog." If a refactor replaces a switch with a lookup table, say that — don't call it "forward chaining rules." Reserve impressive terminology for genuinely novel capabilities. See `docs/plans/PROLOG_PARADIGM_PLAN.md` "Honest Assessment" for the canonical example of this principle.

## Heuristic Policy

- **NEVER implement heuristics without explicit user approval.** A heuristic is any behavior that assumes intent or suppresses diagnostics based on convention rather than proof (e.g., "assume non-negative", "assume callee writes through pointer"). Always present the tradeoff and get approval before implementing.
- **Document all heuristics in `docs/HEURISTICS.md`** — not just in code comments. Each entry must state: (1) what the heuristic does, (2) the tradeoff (what false positives it eliminates vs. what true positives it may mask), and (3) when it was approved.

---

## Build & Test

```bash
# Configure
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18

# Build
cmake --build build -j8

# Run ALL tests locally
ctest --test-dir build --output-on-failure -j8

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

### Remote/Container Environment Setup

In sandboxed environments (e.g., Claude Code remote sessions), `llvm-18-dev`, `libclang-18-dev`, and `libz3-dev` may not be pre-installed and DNS resolution may be blocked. To fix:

```bash
# 1. Resolve archive.ubuntu.com via curl (works through the egress proxy)
IP=$(curl -s --connect-timeout 5 -o /dev/null -w "%{remote_ip}" http://archive.ubuntu.com)

# 2. Add to /etc/hosts so apt can resolve it
sudo sh -c "echo '$IP archive.ubuntu.com' >> /etc/hosts"
sudo sh -c "echo '$IP security.ubuntu.com' >> /etc/hosts"

# 3. Configure apt to use the egress proxy
sudo sh -c "echo 'Acquire::http::Proxy \"'$GLOBAL_AGENT_HTTP_PROXY'\";' > /etc/apt/apt.conf.d/99proxy"
sudo sh -c "echo 'Acquire::https::Proxy \"'$GLOBAL_AGENT_HTTPS_PROXY'\";' >> /etc/apt/apt.conf.d/99proxy"

# 4. Install dev packages
sudo apt-get update -qq
sudo apt-get install -y llvm-18-dev libclang-18-dev libz3-dev
```

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
| `*_silent.cpp` | Out-of-scope cases, compiles silently | No errors expected |

**CMake auto-discovery:** CMakeLists.txt uses `file(GLOB ...)` patterns to auto-discover tests by suffix. New `_pass.cpp`, `_fail.cpp`, `_xfail.cpp`, `_xpass.cpp`, and `_silent.cpp` files in `examples/` are picked up automatically after `cmake -B build`. No manual CMakeLists.txt edits are needed when adding, renaming, or removing test files — just re-run cmake to regenerate.

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

# SARIF output - write SARIF 2.1.0 JSON alongside normal diagnostics
clang++ -fplugin=./build/libDTTPlugin.so -fplugin-arg-dtt---sarif=/tmp/out.sarif -std=c++20 file.cpp
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

### STL Annotations (annotations/std.dtt)

```
# Reusable traits compose into type definitions
trait SizedContainer {
  @ctor(0):
    ensures _size = 0
  empty(0):
    ensures_if_true _size == 0
    ensures_if_false _size == 1
  clear(0):
    ensures _size = 0
  size(0):
    returns _size
}

type vector : SizedContainer, SequenceAccess, PushBack {
  vector(1):
    ensures _size = arg0
  operator[](1):
    requires arg0 < _size
}
```

### C Library Annotations (annotations/libc.dtt)

```
# Multi-function declarations share one body
fn fgetc(1), getc(1), ftell(1), rewind(1), fflush(1),
   feof(1), ferror(1), clearerr(1), fileno(1), ftello(1) {
  requires arg0._nonnull != 0 "FILE* may be null (fopen can fail)"
}

# Pointer resources: deferred on null check
fn fopen(2), tmpfile(0), popen(2) {
  ensures result.@open = FileIO.OPEN when result._nonnull != 0
  scope_exit result.@open == FileIO.CLOSED when result._nonnull != 0
}

# Integer fd resources: deferred on >= 0 check
fn open(2), open(3), mkstemp(1), socket(3), epoll_create(1) {
  ensures result.@fd = FileIO.OPEN when result._value >= 0
  scope_exit result.@fd == FileIO.CLOSED when result._value >= 0
}
```

### Automatic Resource Leak Detection

DTT tracks 40+ resource-related functions across 15+ acquire/release families via ghost fields (`@open`, `@allocated`, `@fd`, etc.):
- **FILE I/O**: fopen/fclose, popen/pclose
- **Memory**: malloc/free, calloc/free, strdup/free
- **File descriptors**: open/close, socket/close, mkstemp/close, epoll_create/close
- **Directories**: opendir/closedir
- **Dynamic libraries**: dlopen/dlclose

Ghost fields use **deferred guard evaluation**: pending postconditions are applied when branch analysis proves the `when` guard. For pointers, `if (f)` proves `_nonnull != 0`; for integer fds, `if (fd >= 0)` proves `_value >= 0`. Ownership transfers on return are recognized; leaks detected at scope exit.

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
| `docs/DTT_SAFETY_ROADMAP.md` | Safety profiles, GSL, Circle insights, implementation roadmap |
| `docs/GAP_CLOSURE_PLAN.md` | All known gaps, xfail/xpass tracking |
| `docs/VALUE_CONCEPTS_SPEC.md` | Bounded quantifiers and collection properties |
| `docs/C_LIBRARY_SUPPORT.md` | C library bounds checking and resource tracking |
| `docs/WINDOWS_FUNCTION_ANNOTATIONS.md` | Windows API annotations |
| `docs/plans/` | Future feature proposals |
| `docs/archive/` | Historical design decisions |
