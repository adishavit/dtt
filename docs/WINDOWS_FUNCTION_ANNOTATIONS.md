# Windows Function Auto-Annotations

> **STATUS: PHASE 1+2 IMPLEMENTED**
>
> Auto-annotate Windows-specific functions for HANDLE lifecycle, COM/OLE type lifetime,
> CRT static-buffer functions, and allocator pairs. Modeled after `src/DTTCLib.cpp`.
>
> **Implemented:** HANDLE lifecycle (CreateFile, CloseHandle, FindFirstFile, FindClose,
> GDI, Sync, Heap, MemoryMapped), COM/OLE allocators (BSTR, CoTaskMem, Local, Global),
> SafeArray create/destroy.
>
> **Known gaps (xfail):** VARIANT init/clear (needs arg-based scope-exit requirements),
> Registry out-param tracking (RegOpenKeyExW writes to HKEY*, not return value).
>
> **Deferred:** Phase 3 (CRT static buffers), Phase 4 (COM ref counting).

---

## Motivation

Windows C/C++ code suffers from recurring use-after-free and dangling-pointer
vulnerabilities rooted in a small number of API patterns. Many of these patterns
are direct analogs to what DTT already tracks for POSIX/C (e.g., `fopen`/`fclose`,
`malloc`/`free`). Adding Windows annotations requires no new tracking machinery
for the first two phases -- only new entries in a registry file.

### Vulnerability Landscape

The five root-cause patterns behind Windows lifetime CVEs:

| Pattern | Example CVEs | DTT Ghost-Field Analog |
|---------|-------------|------------------------|
| HANDLE use-after-close | CVE-2019-0708, CVE-2021-40449, CVE-2016-3310 | `@handle` (like `@fd`) |
| BSTR/SafeArray use-after-free | CVE-2021-26411, CVE-2020-0674, CVE-2019-1367 | `@allocated` (like `malloc`) |
| COM ref-count-to-zero UAF | CVE-2019-0888 | `@refcount` (new) |
| Static-buffer pointer reuse | CWE-676, CERT CON33-C | `@static_buffer` (new) |
| Two-call TOCTOU | CVE-2013-6356, CVE-2010-4398 | Out of scope (temporal) |

---

## Architecture

### File Layout

```
src/
  DTTCLib.cpp          # Existing: C library annotations
  DTTCLib.h            # Existing: registerCLibAnnotations() declaration
  DTTStdLib.cpp        # Existing: C++ stdlib annotations
  DTTWinLib.cpp        # New: Windows API annotations
  DTTWinLib.h          # New: registerWindowsAnnotations() declaration
examples/
  win_api.h            # New: Mock Windows API declarations for -fsyntax-only testing
  1300_win_handle_pass.cpp
  1301_win_handle_fail.cpp
  ...
```

### Always Register (No Conditional Compilation)

DTT matches functions by **name + arity** only (`FreeFunctionKey{fn, numParams}`).
Annotations for functions that don't appear in the translation unit are completely
inert -- they sit in the `DenseMap` and never match. There is no need for target
triple checks or `#ifdef _WIN32` guards.

This also means Linux CI can test Windows annotations using mock headers with
`-fsyntax-only` (no linking, no Windows SDK needed). Several existing tests
already work this way (e.g., `examples/18_z3_arithmetic_pass.cpp` has no
`#include` at all).

### Registration Call Site

In `src/DTTPlugin.cpp` at `loadSyntheticAnnotations()` (line ~2829):

```cpp
void loadSyntheticAnnotations() {
    dtt::registerSyntheticAnnotations();  // C++ stdlib
    dtt::registerCLibAnnotations();       // C library
    dtt::registerWindowsAnnotations();    // Windows API  <-- ADD THIS
    // ... copy to internal maps ...
}
```

The existing loop at line ~2903 already copies all `dtt::FreeFunctionAnnotations`
entries into internal maps, so no further changes are needed in `DTTPlugin.cpp`.

### Registration Pattern (Exact Types)

`DTTWinLib.cpp` uses the same types as `DTTCLib.cpp`. The exact structures:

```cpp
// From DTTTypes.h:
struct FreeFunctionKey { const char* functionName; unsigned numParams; };

struct FreeFunctionInfo {
    SmallVector<FreeFunctionPrecondition, 2> preconditions;
    SmallVector<FreeFunctionPostcondition, 2> postconditions;       // arg postconds
    SmallVector<FreeFunctionResultPost, 2>    resultPostconditions; // return value
    SmallVector<FreeFunctionScopeExitReq, 2>  scopeExitRequires;   // leak detection
};

struct FreeFunctionPrecondition {
    int argIndex;          // Which argument to check
    const char* field;     // Ghost field to check (e.g., "@handle")
    int64_t forbiddenValue;// Value that violates precondition
    ProofOp checkOp;       // Typically NE
    const char* errorReason;
    bool checkLifetime;    // For use-after-free checks
};

struct FreeFunctionPostcondition {
    int argIndex;          // Which argument to modify
    const char* field;     // Ghost field to set
    int64_t value;         // Value to set
};

struct FreeFunctionResultPost {
    const char* field;     // Ghost field to set on return value
    int64_t value;
};

struct FreeFunctionScopeExitReq {
    const char* field;     // Ghost field that must reach required value
    ProofOp op;            // Typically EQ
    int64_t requiredValue; // Typically 0 (resource must be released)
};
```

### A/W Suffix Registration

Most Win32 functions exist in `A` (ANSI) and `W` (wide) variants with identical
signatures. The implementation registers both variants explicitly with string
literals (which have static lifetime, so `FreeFunctionKey` pointers remain valid).
This is simpler than an intern helper and avoids extra infrastructure.

---

## Phased Implementation

### Phase 1: HANDLE Lifecycle -- IMPLEMENTED

**Fits existing model.** Direct parallel to `@fd` / `@open` tracking in `DTTCLib.cpp`.

A ghost field `@handle` tracks whether a Win32 HANDLE is valid. Functions that
create handles set `@handle = 1`; functions that close them require `@handle == 1`
as a precondition and set `@handle = 0` as a postcondition.

#### General HANDLE Functions

| Function | Pre | Post | Scope-Exit |
|----------|-----|------|------------|
| `CreateFileA/W` (7 args) | -- | `ret.@handle = 1` | `@handle == 0` |
| `CloseHandle` (1 arg) | `arg0._nonnull != 0` | `arg0.@handle = 0`, `arg0._nonnull = 0` | -- |

#### File Search

| Function | Pre | Post | Scope-Exit |
|----------|-----|------|------------|
| `FindFirstFileA/W` (2 args) | -- | `ret.@handle = 1` | `@handle == 0` |
| `FindNextFileA/W` (2 args) | `arg0._nonnull != 0` | -- | -- |
| `FindClose` (1 arg) | `arg0._nonnull != 0` | `arg0.@handle = 0`, `arg0._nonnull = 0` | -- |

#### Registry -- KNOWN GAP (xfail)

> **Limitation:** `RegOpenKeyExW` writes the new HKEY to an out-param (`HKEY*`),
> but `FreeFunctionScopeExitReq` only works on return values. The postcondition
> on arg4 sets `(&hKey).@regkey = 1`, while `RegCloseKey(hKey)` checks `hKey` --
> different variables. See `1340_win_registry_xfail.cpp`.

| Function | Arity | Pre | Post | Scope-Exit |
|----------|-------|-----|------|------------|
| `RegOpenKeyExA/W` | 5 | -- | `ret.@regkey = 1` | `@regkey == 0` |
| `RegCreateKeyExA/W` | 9 | -- | `ret.@regkey = 1` | `@regkey == 0` |
| `RegCloseKey` | 1 | `arg0.@regkey == 1` | `arg0.@regkey = 0` | -- |
| `RegQueryValueExA/W` | 6 | `arg0.@regkey == 1` | -- | -- |
| `RegSetValueExA/W` | 6 | `arg0.@regkey == 1` | -- | -- |

#### GDI Objects

| Function | Arity | Pre | Post | Scope-Exit |
|----------|-------|-----|------|------------|
| `CreateCompatibleDC` | 1 | -- | `ret.@handle = 1` | `@handle == 0` |
| `DeleteDC` | 1 | `arg0._nonnull != 0` | `arg0.@handle = 0`, `arg0._nonnull = 0` | -- |
| `DeleteObject` | 1 | `arg0._nonnull != 0` | `arg0.@handle = 0`, `arg0._nonnull = 0` | -- |

#### Synchronization

| Function | Arity | Pre | Post | Scope-Exit |
|----------|-------|-----|------|------------|
| `CreateEventA/W` | 4 | -- | `ret.@handle = 1` | `@handle == 0` |
| `CreateMutexA/W` | 3 | -- | `ret.@handle = 1` | `@handle == 0` |
| `CreateSemaphoreA/W` | 4 | -- | `ret.@handle = 1` | `@handle == 0` |

All synchronization HANDLEs are closed with `CloseHandle` (already annotated above).

#### Heap

| Function | Arity | Pre | Post | Scope-Exit |
|----------|-------|-----|------|------------|
| `HeapCreate` | 3 | -- | `ret.@heap = 1` | `@heap == 0` |
| `HeapDestroy` | 1 | `arg0._nonnull != 0` | `arg0.@heap = 0`, `arg0._nonnull = 0` | -- |
| `HeapAlloc` | 3 | -- | `ret.@allocated = 1` | `@allocated == 0` |
| `HeapFree` | 3 | -- | `arg2.@allocated = 0` | -- |

#### Memory-Mapped Files

| Function | Arity | Pre | Post | Scope-Exit |
|----------|-------|-----|------|------------|
| `CreateFileMappingA/W` | 6 | -- | `ret.@handle = 1` | `@handle == 0` |
| `MapViewOfFile` | 5 | -- | `ret.@mapped = 1` | `@mapped == 0` |
| `UnmapViewOfFile` | 1 | `arg0._nonnull != 0` | `arg0.@mapped = 0`, `arg0._nonnull = 0` | -- |

#### Implementation (Exact Code)

```cpp
// DTTWinLib.cpp
#include "DTTWinLib.h"

namespace dtt {

void registerWindowsAnnotations() {
    auto addResourceAcquire = [](const char* fn, unsigned arity,
                                  const char* field, int64_t acquireValue,
                                  int64_t releaseValue) {
        FreeFunctionInfo info;
        info.resultPostconditions.push_back({field, acquireValue});
        info.scopeExitRequires.push_back({field, ProofOp::EQ, releaseValue});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // Close with _nonnull precondition (fclose pattern).
    // Checks _nonnull before close, invalidates both ghost field and _nonnull
    // after (double-close detection).
    auto addResourceClose = [](const char* fn, unsigned arity, int argIdx,
                                const char* field, int64_t releaseValue,
                                const char* errorMsg) {
        FreeFunctionInfo info;
        info.preconditions.push_back({
            argIdx, "_nonnull", 0, ProofOp::NE, errorMsg
        });
        info.postconditions.push_back({argIdx, field, releaseValue});
        info.postconditions.push_back({argIdx, "_nonnull", 0});
        FreeFunctionAnnotations[{fn, arity}] = info;
    };

    // HANDLE lifecycle
    addResourceAcquire("CreateFileA", 7, "@handle", 1, 0);
    addResourceAcquire("CreateFileW", 7, "@handle", 1, 0);

    addResourceClose("CloseHandle", 1, 0, "@handle", 0,
                     "HANDLE may be invalid or already closed");

    addResourceAcquire("FindFirstFileA", 2, "@handle", 1, 0);
    addResourceAcquire("FindFirstFileW", 2, "@handle", 1, 0);

    addResourceClose("FindClose", 1, 0, "@handle", 0,
                     "HANDLE may be invalid or already closed");

    // ... (GDI, sync, heap, mmap, COM/OLE follow same pattern)
}

} // namespace dtt
```

#### CVE Evidence

| CVE | CVSS | Description |
|-----|------|-------------|
| **CVE-2019-0708** (BlueKeep) | 9.8 | RDP channel object freed, dangling pointer persists. |
| **CVE-2021-40449** | 7.8 | `ResetDC` frees DC during callback; caller uses dangling DC. In the wild. |
| **CVE-2016-3310** | 7.8 | `DeleteObject` frees GDI surface; `NtGdiStartPage` reuses invalid handle. |
| **CVE-2022-21972** | 8.1 | `raspptp.sys` ref count race: zero while another thread holds pointer. |
| **CVE-2025-24983** | 7.0 | Win32k window/DC object UAF. Actively exploited. |

---

### Phase 2: COM/OLE Allocator Pairs (BSTR, SafeArray, CoTaskMem, Local) -- IMPLEMENTED

**Fits existing model.** Direct parallel to `malloc`/`free` in `DTTCLib.cpp`.

#### BSTR

| Function | Arity | Pre | Post | Scope-Exit |
|----------|-------|-----|------|------------|
| `SysAllocString` | 1 | -- | `ret.@allocated = 1` | `@allocated == 0` |
| `SysAllocStringLen` | 2 | -- | `ret.@allocated = 1` | `@allocated == 0` |
| `SysAllocStringByteLen` | 2 | -- | `ret.@allocated = 1` | `@allocated == 0` |
| `SysFreeString` | 1 | -- | `arg0.@allocated = 0` | -- |

#### SafeArray -- IMPLEMENTED (create/destroy only)

| Function | Arity | Pre | Post | Scope-Exit | Status |
|----------|-------|-----|------|------------|--------|
| `SafeArrayCreate` | 3 | -- | `ret.@safearray = 1` | `@safearray == 0` | Implemented |
| `SafeArrayCreateVector` | 3 | -- | `ret.@safearray = 1` | `@safearray == 0` | Implemented |
| `SafeArrayDestroy` | 1 | `_nonnull != 0` | `arg0.@safearray = 0`, `_nonnull = 0` | -- | Implemented |
| `SafeArrayAccessData` | 2 | `arg0.@sa_valid == 1` | `arg0.@sa_locked = 1` | -- | Deferred |
| `SafeArrayUnaccessData` | 1 | `arg0.@sa_locked == 1` | `arg0.@sa_locked = 0` | -- | Deferred |
| `SafeArrayGetElement` | 3 | `arg0.@sa_valid == 1` | -- | -- | Deferred |
| `SafeArrayPutElement` | 3 | `arg0.@sa_valid == 1` | -- | -- | Deferred |

#### VARIANT -- KNOWN GAP (xfail)

> **Limitation:** `VariantInit` sets state on an argument (not return value),
> and `FreeFunctionScopeExitReq` only works on return values. DTT cannot enforce
> that `VariantClear` is called before scope exit. See `1320_win_variant_xfail.cpp`.

| Function | Arity | Pre | Post | Status |
|----------|-------|-----|------|--------|
| `VariantInit` | 1 | -- | `arg0.@var_valid = 1` | xfail (needs arg scope-exit) |
| `VariantClear` | 1 | `arg0.@var_valid == 1` | `arg0.@var_valid = 0` | xfail (needs arg scope-exit) |

#### CoTaskMem / LocalAlloc / GlobalAlloc

| Function | Arity | Pre | Post | Scope-Exit |
|----------|-------|-----|------|------------|
| `CoTaskMemAlloc` | 1 | -- | `ret.@allocated = 1` | `@allocated == 0` |
| `CoTaskMemFree` | 1 | -- | `arg0.@allocated = 0` | -- |
| `LocalAlloc` | 2 | -- | `ret.@allocated = 1` | `@allocated == 0` |
| `LocalFree` | 1 | -- | `arg0.@allocated = 0` | -- |
| `GlobalAlloc` | 2 | -- | `ret.@allocated = 1` | `@allocated == 0` |
| `GlobalFree` | 1 | -- | `arg0.@allocated = 0` | -- |

Note: `SysFreeString`, `CoTaskMemFree`, `LocalFree`, and `GlobalFree` accept
NULL safely, so they use `addResourceRelease` (no precondition) instead of
`addResourceClose`. `FormatMessage` with `FORMAT_MESSAGE_ALLOCATE_BUFFER`
internally calls `LocalAlloc`. Annotating it requires flag-dependent
postconditions. Deferred.

#### CVE Evidence

| CVE | CVSS | Description |
|-----|------|-------------|
| **CVE-2021-26411** | 8.8 | IE `mshtml`: BSTR freed, accessed through VARIANT array. In the wild. |
| **CVE-2020-0674** | 7.5 | JScript: BSTR UAF via GC gap in `Array.sort` callback. In the wild. |
| **CVE-2019-1367** | 7.5 | JScript: Same bug class as CVE-2020-0674 via `arguments[]`. In the wild. |
| **CVE-2019-1208** | 7.5 | VBScript: SafeArray descriptor freed, Variant holds dangling pointer. |
| **CVE-2018-8174** | 7.5 | VBScript: Array deletion leaves dangling pointer to BSTR. In the wild. |
| **CVE-2019-0888** | 8.8 | ADO COM: vtable deref after C++ object freed. |
| **CVE-2025-30397** | 7.5 | JScript: Improper object reference management causes UAF. |

Google Project Zero's ["Deja vu-lnerability"](https://projectzero.google/2021/02/deja-vu-lnerability.html)
analysis: four JScript CVEs (CVE-2018-8653, CVE-2019-1367, CVE-2019-1429,
CVE-2020-0674) share the same root cause, same exploitation method (BSTR heap
manipulation), all exploited in the wild by the same threat actor.

---

### Phase 3 (Deferred): CRT Static-Buffer Functions

**Requires new concept.** Returns pointers to internal static storage overwritten
by next call.

| Function | Secure Variant | Problem |
|----------|----------------|---------|
| `strtok` | `strtok_s` | Pointer to internal static buffer |
| `asctime` | `asctime_s` | Pointer to static `char[26]` |
| `gmtime` | `gmtime_s` | Pointer to static `struct tm` |
| `localtime` | `localtime_s` | Pointer to static `struct tm` |
| `getenv` | `_dupenv_s` | Pointer to environment block; invalidated by `_putenv` |

Requires DTT to track "which function family produced this static pointer" and
invalidate all live pointers from the same family on re-entry. New machinery.

Evidence: CWE-676, CERT CON33-C, CERT MSC33-C.

---

### Phase 4 (Deferred): COM Reference Counting

**Requires integer ghost-field tracking.** `IUnknown::AddRef`/`Release` manipulate
an integer reference count. UAF occurs when `Release` decrements to zero while
another pointer exists.

Requires reasoning about `@refcount > 0`, not `@refcount == 1`.

Evidence: CVE-2019-0888, numerous IE/Edge CVEs.

---

### Out of Scope: Two-Call TOCTOU

The query-size-then-fill pattern (`GetEnvironmentVariable`, `RegQueryValueEx`,
`GetWindowTextLength` + `GetWindowText`) is a temporal ordering problem outside
DTT's current precondition/postcondition model.

Evidence: CVE-2013-6356, CVE-2010-4398, CVE-2025-29969.

---

## Testing

### Mock Header (`examples/win_api.h`)

Tests use mock declarations -- no Windows SDK required:

See `examples/win_api.h` for the full mock header. It provides minimal Win32
type definitions and function declarations sufficient for `-fsyntax-only` testing
without a Windows SDK.

### Test Files (examples/ folder, 1300+ range)

| File | Type | Status | What It Tests |
|------|------|--------|---------------|
| `1300_win_handle_pass.cpp` | _pass | Passing | CreateFile/CloseHandle, FindFirstFile/FindClose, GDI, sync, heap, mmap |
| `1301_win_handle_fail.cpp` | _fail | Passing | HANDLE leak, double-close, CloseHandle(nullptr), GDI leak, event leak |
| `1310_win_bstr_pass.cpp` | _pass | Passing | SysAllocString/SysFreeString, CoTaskMem, LocalAlloc, GlobalAlloc |
| `1311_win_bstr_fail.cpp` | _fail | Passing | BSTR leak, CoTaskMem leak, LocalAlloc leak, GlobalAlloc leak |
| `1312_win_safearray_pass.cpp` | _pass | Passing | SafeArrayCreate/SafeArrayDestroy, SafeArrayCreateVector |
| `1313_win_safearray_fail.cpp` | _fail | Passing | SafeArray leak, double SafeArrayDestroy |
| `1320_win_variant_xfail.cpp` | _xfail | Gap exists | VariantInit without VariantClear (needs arg scope-exit) |
| `1340_win_registry_xfail.cpp` | _xfail | Gap exists | RegOpenKeyExW without RegCloseKey (out-param limitation) |

Each test includes multiple variants (per CLAUDE.md rule: "Multiple variants per
gap, ALL in the same test file").

### CMake Integration

```cmake
# Add Windows annotation tests (use mock header, no Windows SDK needed)
set(WIN_TESTS
    1300_win_handle_pass
    1301_win_handle_fail
    # ... etc
)

foreach(test_name ${WIN_TESTS})
    set(test_file "${CMAKE_SOURCE_DIR}/examples/${test_name}.cpp")
    if(test_name MATCHES "_fail$")
        add_test(NAME ${test_name}
            COMMAND ${CLANGXX} ${plugin_args} ${stdlib_flag} -std=c++20 -fsyntax-only ${test_file})
        set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)
    else()
        add_test(NAME ${test_name}
            COMMAND ${CLANGXX} ${plugin_args} ${stdlib_flag} -std=c++20 -fsyntax-only ${test_file})
    endif()
endforeach()
```

---

## Implementation Steps

Following TDD workflow per CLAUDE.md / TDD.md:

### Step 1: Create `examples/win_api.h` mock header

### Step 2: Write tests FIRST (TDD)

For each function group (HANDLE, BSTR, etc.):

1. Write `_pass.cpp` test with correct usage patterns (multiple variants)
2. Write `_fail.cpp` test with buggy patterns (use-after-close, double-close, leak)
3. Run tests -- confirm `_pass` tests PASS and `_fail` tests PASS (since no
   annotations exist yet, DTT won't catch the bugs, so `_fail` tests compile
   cleanly = test "passes" = WILL_FAIL makes it report as "failed" -- which is
   what we want before implementing)

### Step 3: Create `src/DTTWinLib.h` and `src/DTTWinLib.cpp`

Register all Phase 1 + Phase 2 annotations using the exact `FreeFunctionInfo`
types shown above.

### Step 4: Wire up in `DTTPlugin.cpp`

Add `#include "DTTWinLib.h"` and call `dtt::registerWindowsAnnotations()` in
`loadSyntheticAnnotations()`.

### Step 5: Add to `CMakeLists.txt`

Add `src/DTTWinLib.cpp` to the plugin's source list and register the test files.

### Step 6: Run tests -- confirm all pass

`_pass.cpp` tests should still compile cleanly. `_fail.cpp` tests should now
trigger DTT errors (compilation fails = WILL_FAIL test passes).

### Step 7: Run full test suite

```bash
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build
ctest --test-dir build --output-on-failure
```

Ensure no regressions in existing tests.

---

## Summary

| Phase | Scope | Ghost Fields | Machinery | CVE Count | Status |
|-------|-------|-------------|-----------|-----------|--------|
| 1 | HANDLE lifecycle | `@handle`, `@heap`, `@mapped` | Existing pattern | 5+ | **IMPLEMENTED** |
| 2 | COM/OLE allocators | `@allocated`, `@safearray` | Existing pattern | 7+ | **IMPLEMENTED** |
| 2b | VARIANT init/clear | `@var_valid` | Needs arg scope-exit | 7+ | xfail |
| 2c | Registry out-param | `@regkey` | Needs out-param tracking | 5+ | xfail |
| 3 | CRT static buffers | `@static_buffer` | Function-family invalidation | CWE-676 class | Deferred |
| 4 | COM ref counting | `@refcount` | Integer ghost-field constraints | 2+ | Deferred |
