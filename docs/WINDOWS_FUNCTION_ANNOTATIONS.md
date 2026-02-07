# Windows Function Auto-Annotations

> **STATUS: PROPOSAL**
>
> Auto-annotate Windows-specific functions for HANDLE lifecycle, COM/OLE type lifetime,
> CRT static-buffer functions, and allocator pairs. Modeled after `DTTCLib.cpp`.

---

## Motivation

Windows C/C++ code suffers from recurring use-after-free and dangling-pointer
vulnerabilities rooted in a small number of API patterns. Many of these patterns
are direct analogs to what DTT already tracks for POSIX/C (e.g., `fopen`/`fclose`,
`malloc`/`free`). Adding Windows annotations requires no new tracking machinery
for the first two tiers -- only new entries in a registry file.

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

## Phased Implementation

### Phase 1: HANDLE Lifecycle

**Fits existing model.** Direct parallel to `@fd` / `@open` tracking in `DTTCLib.cpp`.

A new ghost field `@handle` tracks whether a Win32 HANDLE is valid. Functions that
create handles set `@handle = 1`; functions that close them require `@handle == 1`
as a precondition and set `@handle = 0` as a postcondition.

#### General HANDLE Functions

| Function | Pre | Post | Notes |
|----------|-----|------|-------|
| `CreateFileA/W` | -- | `ret.@handle = (ret != INVALID_HANDLE_VALUE)` | File, device, pipe, etc. |
| `CloseHandle` | `arg0.@handle == 1` | `arg0.@handle = 0` | Generic close |
| `DuplicateHandle` | `arg0.@handle == 1` | `arg4.@handle = 1` | Source must be valid |

#### File Search

| Function | Pre | Post |
|----------|-----|------|
| `FindFirstFileA/W` | -- | `ret.@handle = (ret != INVALID_HANDLE_VALUE)` |
| `FindNextFileA/W` | `arg0.@handle == 1` | -- |
| `FindClose` | `arg0.@handle == 1` | `arg0.@handle = 0` |

#### Registry

| Function | Pre | Post |
|----------|-----|------|
| `RegOpenKeyExA/W` | -- | `arg4.@regkey = 1` (out param) |
| `RegCreateKeyExA/W` | -- | `arg5.@regkey = 1` (out param) |
| `RegCloseKey` | `arg0.@regkey == 1` | `arg0.@regkey = 0` |
| `RegQueryValueExA/W` | `arg0.@regkey == 1` | -- |
| `RegSetValueExA/W` | `arg0.@regkey == 1` | -- |

#### GDI Objects

| Function | Pre | Post |
|----------|-----|------|
| `CreateCompatibleDC` | -- | `ret.@handle = 1` |
| `DeleteDC` | `arg0.@handle == 1` | `arg0.@handle = 0` |
| `SelectObject` | `arg0.@handle == 1`, `arg1.@handle == 1` | -- |
| `DeleteObject` | `arg0.@handle == 1` | `arg0.@handle = 0` |
| `CreatePen`, `CreateBrushIndirect`, `CreateFont` | -- | `ret.@handle = 1` |

#### Synchronization

| Function | Pre | Post |
|----------|-----|------|
| `CreateEventA/W` | -- | `ret.@handle = 1` |
| `CreateMutexA/W` | -- | `ret.@handle = 1` |
| `CreateSemaphoreA/W` | -- | `ret.@handle = 1` |
| `OpenEvent/Mutex/Semaphore` | -- | `ret.@handle = 1` |
| `ReleaseMutex` | `arg0.@handle == 1` | -- |
| `ReleaseSemaphore` | `arg0.@handle == 1` | -- |

All synchronization HANDLEs are closed with `CloseHandle` (already annotated above).

#### Heap

| Function | Pre | Post |
|----------|-----|------|
| `HeapCreate` | -- | `ret.@heap = 1` |
| `HeapDestroy` | `arg0.@heap == 1` | `arg0.@heap = 0` |
| `HeapAlloc` | `arg0.@heap == 1` | `ret.@allocated = 1` |
| `HeapFree` | `arg0.@heap == 1`, `arg2.@allocated == 1` | `arg2.@allocated = 0` |
| `HeapReAlloc` | `arg0.@heap == 1`, `arg2.@allocated == 1` | `ret.@allocated = 1` |

#### Memory-Mapped Files

| Function | Pre | Post |
|----------|-----|------|
| `CreateFileMappingA/W` | `arg0.@handle == 1` | `ret.@handle = 1` |
| `MapViewOfFile` | `arg0.@handle == 1` | `ret.@mapped = 1` |
| `UnmapViewOfFile` | `arg0.@mapped == 1` | `arg0.@mapped = 0` |

#### Scope-Exit Constraints

```
dtt:~requires="@handle == 0"    // HANDLE must be closed before scope exit
dtt:~requires="@regkey == 0"    // Registry key must be closed
dtt:~requires="@heap == 0"      // Heap must be destroyed
dtt:~requires="@mapped == 0"    // View must be unmapped
```

#### CVE Evidence

| CVE | CVSS | Description | Pattern |
|-----|------|-------------|---------|
| **CVE-2019-0708** (BlueKeep) | 9.8 | RDP channel object freed, dangling pointer persists. Attacker sprays kernel pool to hijack control flow. | HANDLE-like object use-after-free |
| **CVE-2021-40449** | 7.8 | `ResetDC` frees device context during user-mode callback; caller uses dangling DC. Exploited in the wild (Kaspersky discovery). | GDI HANDLE use-after-close |
| **CVE-2016-3310** | 7.8 | `DeleteObject` frees GDI surface; `NtGdiStartPage` then uses the invalid handle. | GDI object use-after-delete |
| **CVE-2022-21972** | 8.1 | `raspptp.sys` reference count race: count hits zero while another thread holds pointer. | Kernel HANDLE lifetime race |
| **CVE-2025-24983** | 7.0 | Win32k window/DC object UAF. Actively exploited. | Win32k HANDLE UAF |

---

### Phase 2: COM/OLE Allocator Pairs (BSTR, SafeArray, CoTaskMem, Local)

**Fits existing model.** Direct parallel to `malloc`/`free` in `DTTCLib.cpp`.

#### BSTR

| Function | Pre | Post |
|----------|-----|------|
| `SysAllocString` | -- | `ret.@allocated = 1` |
| `SysAllocStringLen` | -- | `ret.@allocated = 1` |
| `SysAllocStringByteLen` | -- | `ret.@allocated = 1` |
| `SysReAllocString` | `arg0.@allocated == 1` | `arg0.@allocated = 1` |
| `SysFreeString` | `arg0.@allocated == 1` | `arg0.@allocated = 0` |
| `SysStringLen` | `arg0.@allocated == 1` | -- |
| `SysStringByteLen` | `arg0.@allocated == 1` | -- |

#### SafeArray

| Function | Pre | Post |
|----------|-----|------|
| `SafeArrayCreate` | -- | `ret.@sa_valid = 1` |
| `SafeArrayCreateVector` | -- | `ret.@sa_valid = 1` |
| `SafeArrayDestroy` | `arg0.@sa_valid == 1` | `arg0.@sa_valid = 0` |
| `SafeArrayAccessData` | `arg0.@sa_valid == 1` | `arg0.@sa_locked = 1` |
| `SafeArrayUnaccessData` | `arg0.@sa_locked == 1` | `arg0.@sa_locked = 0` |
| `SafeArrayGetElement` | `arg0.@sa_valid == 1` | -- |
| `SafeArrayPutElement` | `arg0.@sa_valid == 1` | -- |

The `@sa_locked` field models the SafeArray lock: `SafeArrayAccessData` returns a
raw pointer into the array's data. That pointer is only valid while the array is
locked. Using `SafeArrayUnaccessData` unlocks the array; the raw pointer becomes
dangling.

#### VARIANT

| Function | Pre | Post |
|----------|-----|------|
| `VariantInit` | -- | `arg0.@var_valid = 1` |
| `VariantClear` | `arg0.@var_valid == 1` | `arg0.@var_valid = 0` |
| `VariantCopy` | `arg1.@var_valid == 1` | `arg0.@var_valid = 1` |
| `VariantChangeType` | `arg1.@var_valid == 1` | `arg0.@var_valid = 1` |

#### CoTaskMem / LocalAlloc

| Function | Pre | Post |
|----------|-----|------|
| `CoTaskMemAlloc` | -- | `ret.@allocated = 1` |
| `CoTaskMemRealloc` | `arg0.@allocated == 1` | `ret.@allocated = 1` |
| `CoTaskMemFree` | `arg0.@allocated == 1` | `arg0.@allocated = 0` |
| `LocalAlloc` | -- | `ret.@allocated = 1` |
| `LocalReAlloc` | `arg0.@allocated == 1` | `ret.@allocated = 1` |
| `LocalFree` | `arg0.@allocated == 1` | `arg0.@allocated = 0` |
| `GlobalAlloc` | -- | `ret.@allocated = 1` |
| `GlobalReAlloc` | `arg0.@allocated == 1` | `ret.@allocated = 1` |
| `GlobalFree` | `arg0.@allocated == 1` | `arg0.@allocated = 0` |

Note: `FormatMessage` with `FORMAT_MESSAGE_ALLOCATE_BUFFER` internally calls
`LocalAlloc`. The output buffer must be freed with `LocalFree`. This is a common
source of leaks, though annotating `FormatMessage` itself requires flag-dependent
postconditions (only allocates when the flag is set). Deferred to Phase 2b.

#### CVE Evidence

| CVE | CVSS | Description | Pattern |
|-----|------|-------------|---------|
| **CVE-2021-26411** | 8.8 | IE `mshtml`: BSTR freed via `CAttrArray::DeleteAt`, then accessed through VARIANT array. Exploited in the wild. | BSTR use-after-`SysFreeString` |
| **CVE-2020-0674** | 7.5 | JScript: GC doesn't track named args during `Array.sort` callback. Freed BSTR reused via `SysFreeString` + heap spray. Exploited in the wild. | BSTR use-after-free |
| **CVE-2019-1367** | 7.5 | JScript: Same bug class as CVE-2020-0674 but via `arguments[]` array. Exploited in the wild. | BSTR use-after-free |
| **CVE-2019-1208** | 7.5 | VBScript: `SafeArrayReleaseDescriptor` frees descriptor; `Variant` still holds pointer. Attacker fakes a 0x7ffffffff-element SafeArray. | SafeArray use-after-destroy |
| **CVE-2018-8174** | 7.5 | VBScript: Array deletion leaves dangling pointer to freed BSTR. Exploited in the wild by RigEK. | BSTR dangling pointer |
| **CVE-2019-0888** | 8.8 | ADO COM: vtable pointer dereferenced after C++ object freed. | COM object use-after-free |
| **CVE-2025-30397** | 7.5 | JScript: Improper object reference management causes UAF. Recent. | COM/BSTR UAF |

Google Project Zero's ["Deja vu-lnerability"](https://projectzero.google/2021/02/deja-vu-lnerability.html)
analysis documented four JScript CVEs (CVE-2018-8653, CVE-2019-1367, CVE-2019-1429,
CVE-2020-0674) sharing the same root cause and exploitation method (BSTR heap
manipulation), all exploited in the wild by the same threat actor.

---

### Phase 3: CRT Static-Buffer Functions

**Requires new concept.** These functions return pointers to internal static storage
that is overwritten by the next call to the same function.

#### Functions

| Function | Secure Variant | Return Type | Problem |
|----------|----------------|-------------|---------|
| `strtok` | `strtok_s` | `char*` | Pointer to internal static buffer |
| `asctime` | `asctime_s` | `char*` | Pointer to static `char[26]` |
| `ctime` | `ctime_s` | `char*` | Wraps `asctime` |
| `gmtime` | `gmtime_s` | `struct tm*` | Pointer to static `struct tm` |
| `localtime` | `localtime_s` | `struct tm*` | Pointer to static `struct tm` |
| `tmpnam` | `tmpnam_s` | `char*` | Pointer to static buffer |
| `getenv` | `_dupenv_s` | `char*` | Pointer to environment block; invalidated by `_putenv` |

#### Proposed Tracking Model

Introduce a `@static_buffer` ghost field with function-family scoping:

```
// On call to gmtime():
//   ret.@static_buffer = "gmtime"   -- marks which function owns the buffer
//
// On subsequent call to gmtime():
//   Any live variable with @static_buffer == "gmtime" gets invalidated
//   (set to @static_buffer = 0, @allocated = 0)
```

This requires DTT to track "which function family produced this static pointer"
and invalidate all live pointers from the same family on re-entry. This is new
machinery but is a focused extension to the existing ghost-field model.

#### Standards Evidence

- **CWE-676**: Use of Potentially Dangerous Function
- **CERT CON33-C**: Avoid race conditions when using library functions
  (static-buffer functions are thread-unsafe by definition)
- **CERT MSC33-C**: Do not pass invalid data to the `asctime()` function
- Microsoft deprecated all variants and requires `_CRT_SECURE_NO_WARNINGS` to use them
- `clang-tidy` `bugprone-unsafe-functions` check flags all of them

---

### Phase 4 (Deferred): COM Reference Counting

**Requires integer ghost-field tracking.** COM uses `AddRef`/`Release` pairs that
manipulate an integer reference count. Use-after-free occurs when `Release`
decrements to zero (freeing the object) while another pointer still exists.

| Method | Effect |
|--------|--------|
| `IUnknown::AddRef` | `@refcount += 1` |
| `IUnknown::Release` | `@refcount -= 1`; if `@refcount == 0`, object is freed |

DTT currently tracks boolean ghost fields (0/1). Integer tracking (`@refcount`)
is architecturally harder: it requires reasoning about `@refcount > 0` as the
validity precondition, not `@refcount == 1`.

**CVE motivation:** CVE-2019-0888 (ADO COM vtable UAF), numerous IE/Edge CVEs.

**Deferred until** DTT supports integer-valued ghost-field constraints.

---

### Out of Scope: Two-Call TOCTOU

The query-size-then-fill pattern (`GetEnvironmentVariable`, `RegQueryValueEx`,
`GetWindowTextLength` + `GetWindowText`) is a temporal ordering problem. The
vulnerability occurs when the underlying data changes between two calls, causing
a buffer overflow or stale-size read.

This requires modeling temporal ordering between distinct call sites, which is
outside DTT's current precondition/postcondition framework. Noted here for
completeness.

**CVE motivation:** CVE-2013-6356 (`RegQueryValueExW` buffer reuse), CVE-2010-4398
(`RtlQueryRegistryValues` stack overflow), CVE-2025-29969/CVE-2025-55236/CVE-2025-55696
(kernel TOCTOU races).

---

## Architecture

### File Layout

```
src/
  DTTCLib.cpp          # Existing: C library annotations
  DTTStdLib.cpp        # Existing: C++ stdlib annotations
  DTTWinLib.cpp        # New: Windows API annotations
  DTTWinLib.h          # New: registerWindowsAnnotations() declaration
```

### Conditional Compilation

Windows annotations should be registered conditionally. Two options:

**Option A: Target-based (preferred).** Check the Clang target triple at
registration time. Annotations are compiled into the plugin on all platforms but
only registered when the target is Windows:

```cpp
void DTTConsumer::loadSyntheticAnnotations(const LangOptions &LO,
                                           const TargetInfo &TI) {
  registerSyntheticAnnotations();   // C++ stdlib
  registerCLibAnnotations();        // C library

  StringRef triple = TI.getTargetTriple();
  if (triple.contains("win32") || triple.contains("windows")) {
    registerWindowsAnnotations();   // Windows API
  }
}
```

This is simpler -- one binary, no `#ifdef` branching. Annotations that don't match
any function in the TU are inert (zero overhead beyond the initial map insertion).

**Option B: Build-time.** Guard with `#ifdef _WIN32` in CMakeLists.txt. Produces
a smaller plugin on non-Windows builds but requires build-system branching. DTT
already has platform-specific CMake logic (`CMakeLists.txt:75-84`).

**Recommendation:** Option A. The overhead of unused map entries is negligible, and
it allows cross-compilation scenarios (compiling Windows code on Linux with a
Windows target triple).

### Registration Pattern

Follow the existing pattern in `DTTCLib.cpp`:

```cpp
// DTTWinLib.cpp
void registerWindowsAnnotations() {
  // --- HANDLE lifecycle ---
  registerFreeFunctionAnnotation(
      "CreateFileW", /*paramCount=*/7,
      /*pre=*/{},
      /*post=*/{{ReturnField("@handle"), SetTo(1)}},
      /*scopeExit=*/{{"@handle", MustBe(0)}}
  );

  registerFreeFunctionAnnotation(
      "CloseHandle", /*paramCount=*/1,
      /*pre=*/{{ArgField(0, "@handle"), MustBe(1)}},
      /*post=*/{{ArgField(0, "@handle"), SetTo(0)}},
      /*scopeExit=*/{}
  );

  // --- BSTR lifecycle ---
  registerFreeFunctionAnnotation(
      "SysAllocString", /*paramCount=*/1,
      /*pre=*/{},
      /*post=*/{{ReturnField("@allocated"), SetTo(1)}},
      /*scopeExit=*/{{"@allocated", MustBe(0)}}
  );

  registerFreeFunctionAnnotation(
      "SysFreeString", /*paramCount=*/1,
      /*pre=*/{{ArgField(0, "@allocated"), MustBe(1)}},
      /*post=*/{{ArgField(0, "@allocated"), SetTo(0)}},
      /*scopeExit=*/{}
  );

  // ... etc
}
```

### A/W Suffix Handling

Most Win32 functions exist in `A` (ANSI) and `W` (wide) variants. Both need
annotations. Rather than doubling every entry, use a helper:

```cpp
static void registerAW(StringRef baseName, unsigned paramCount,
                       /* pre, post, scopeExit */) {
  SmallString<64> nameA(baseName);
  nameA += 'A';
  SmallString<64> nameW(baseName);
  nameW += 'W';
  registerFreeFunctionAnnotation(nameA, paramCount, ...);
  registerFreeFunctionAnnotation(nameW, paramCount, ...);
}

// Usage:
registerAW("CreateFile", 7, ...);   // Registers CreateFileA and CreateFileW
registerAW("RegOpenKeyEx", 5, ...); // Registers RegOpenKeyExA and RegOpenKeyExW
```

---

## Testing Strategy

### Test Naming

Following the project convention (`examples/` folder):

```
examples/
  600_win_handle_pass.cpp       # Correct HANDLE open/close
  601_win_handle_fail.cpp       # Use-after-CloseHandle
  602_win_handle_leak_fail.cpp  # HANDLE not closed at scope exit
  610_win_bstr_pass.cpp         # Correct BSTR alloc/free
  611_win_bstr_fail.cpp         # Use-after-SysFreeString
  620_win_safearray_pass.cpp    # Correct SafeArray lifecycle
  621_win_safearray_fail.cpp    # Access after SafeArrayDestroy
  622_win_safearray_lock_fail.cpp  # Use data pointer after UnaccessData
  630_win_gdi_pass.cpp          # Correct GDI object lifecycle
  631_win_gdi_fail.cpp          # Use-after-DeleteObject
  640_win_registry_pass.cpp     # Correct registry key lifecycle
  641_win_registry_fail.cpp     # Use-after-RegCloseKey
```

### CI Considerations

Windows tests require either:
- A Windows CI runner with Clang, or
- Cross-compilation with a Windows target triple from Linux

Since DTT currently runs on Linux CI with `clang++-18`, cross-compilation is the
pragmatic choice. Tests would use mock Windows headers that declare the function
signatures without actual implementations (syntax-only analysis).

```cpp
// examples/win_mock_headers.h -- minimal declarations for syntax-only testing
typedef void* HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)(long long)-1)
typedef unsigned long DWORD;
typedef int BOOL;
typedef wchar_t* BSTR;

extern "C" {
HANDLE CreateFileW(const wchar_t*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
BOOL CloseHandle(HANDLE);
BSTR SysAllocString(const wchar_t*);
void SysFreeString(BSTR);
// ...
}
```

This avoids requiring Windows SDK headers while testing DTT's annotation logic.

---

## Summary

| Phase | Scope | New Ghost Fields | New Machinery | CVE Count |
|-------|-------|-----------------|---------------|-----------|
| 1 | HANDLE lifecycle | `@handle`, `@regkey`, `@heap`, `@mapped` | None (existing pattern) | 5+ |
| 2 | COM/OLE allocators | `@sa_valid`, `@sa_locked`, `@var_valid` | None (existing pattern) | 7+ |
| 3 | CRT static buffers | `@static_buffer` | Function-family invalidation | CWE-676 class |
| 4 | COM ref counting | `@refcount` | Integer ghost-field constraints | 2+ |
