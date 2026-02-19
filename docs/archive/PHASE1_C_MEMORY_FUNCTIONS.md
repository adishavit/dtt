# Phase 1: C Memory Function Bounds Checking

## Executive Summary

Buffer overflows from C memory functions account for a significant portion of CVEs.
Microsoft and Google report ~70% of their security vulnerabilities are memory safety issues,
with [CWE-120 (Buffer Copy without Checking Size)](https://cwe.mitre.org/data/definitions/120.html)
and [CWE-787 (Out-of-bounds Write)](https://cwe.mitre.org/data/definitions/787.html) consistently
ranking in the top 10.

This document specifies DTT support for detecting buffer overflows in C memory functions
at compile time, targeting the most common CVE patterns.

---

## ✅ IMPLEMENTATION COMPLETE

**All phases implemented and tested.** DTT now catches the following CVE patterns:

| CVE Pattern | Function | Detection |
|-------------|----------|-----------|
| CVE-2023-38545 (curl) | `memcpy` with ptr+offset | ✅ Pointer offset tracking |
| CVE-2024-24450 (OpenAirInterface) | `memcpy` unguarded size | ✅ Category C unverified warning |
| CVE-2022-26937 (Windows NFS) | `memmove` unguarded | ✅ Bounds checking |
| CVE-2018-18820 (Icecast) | `snprintf` size mismatch | ✅ Size vs buffer check |
| Moonlight GHSA | `strcpy` unguarded | ✅ Length verification |
| Heartbleed-style | `memcpy` with untrusted size | ✅ Category C unverified warning |

**Test Coverage:** 810-811 (memcpy), 874-881 (all functions + pointer arithmetic)

---

## CVE-Driven Motivation

### Real-World Examples

#### CVE-2023-38545: curl SOCKS5 Heap Buffer Overflow (HIGH SEVERITY)

```c
// Simplified vulnerable pattern from curl
char hostname_buf[255];
size_t hostname_len = get_hostname_length();  // Could be > 255!

// BUG: No check that hostname_len <= sizeof(hostname_buf)
memcpy(hostname_buf, hostname, hostname_len);
```

**Root cause:** `hostname_len` from network input trusted without validation against buffer size.
**Impact:** Remote code execution in libcurl 7.69.0 through 8.3.0.

*Source: [curl CVE-2023-38545](https://curl.se/docs/CVE-2023-38545.html)*

---

#### CVE-2024-24450: OpenAirInterface Stack Buffer Overflow

```c
// Stack-based memcpy buffer overflow
void handle_response(uint8_t *data, size_t data_len) {
    uint8_t local_buf[MAX_BUF];

    // BUG: data_len from network, not validated
    memcpy(local_buf, data, data_len);
}
```

**Root cause:** Network-provided length used directly in memcpy without bounds check.
**Impact:** Denial of service, potential code execution.

*Source: [CVE-2024-24450](https://www.cvedetails.com/cve/CVE-2024-24450/)*

---

#### CVE-2022-26937: Windows NFS memmove Overflow

```c
// Windows NFS vulnerability
char buffer[96];
char *universal_addr = get_client_addr();  // Attacker-controlled

// BUG: universal_addr may exceed 96 bytes
memmove(buffer, universal_addr, strlen(universal_addr));
```

**Root cause:** Client-provided string copied without length validation.
**Impact:** Remote code execution as SYSTEM.

*Source: [ZDI CVE-2022-26937 Analysis](https://www.thezdi.com/blog/2022/6/7/cve-2022-26937-microsoft-windows-network-file-system-nlm-portmap-stack-buffer-overflow)*

---

#### CVE-2018-18820: Icecast snprintf Truncation Bug

```c
char post[4096];
size_t post_offset = 0;

// Multiple snprintf calls
post_offset += snprintf(post + post_offset, sizeof(post) - post_offset, "...");
post_offset += snprintf(post + post_offset, sizeof(post) - post_offset, "...");

// BUG: snprintf returns what it WOULD have written, not what it DID write
// If truncation occurs, post_offset > sizeof(post)
// sizeof(post) - post_offset underflows to huge value!
post_offset += snprintf(post + post_offset, sizeof(post) - post_offset, "...");
```

**Root cause:** snprintf return value misunderstanding leads to size_t underflow.
**Impact:** Stack buffer overflow, code execution.

*Source: [GitHub Security Lab CVE-2018-18820](https://securitylab.github.com/research/cve-2018-18820-snprintf-vulnerability-icecast/)*

---

#### Classic strcpy Pattern (Moonlight, countless others)

```c
// From moonlight-common-c
char versionString[256];
char *serverVersion = get_server_version();  // Untrusted input

// BUG: serverVersion may exceed 256 bytes
strcpy(versionString, serverVersion);
```

*Source: [Moonlight GHSA-f3h8-j898-5h5v](https://github.com/moonlight-stream/moonlight-common-c/security/advisories/GHSA-f3h8-j898-5h5v)*

---

## Target Functions

### Tier 1: Memory Copy Functions (Highest Priority)

| Function | Signature | Preconditions |
|----------|-----------|---------------|
| `memcpy` | `void* memcpy(void* dest, const void* src, size_t n)` | `n <= dest._capacity`, `n <= src._capacity` |
| `memmove` | `void* memmove(void* dest, const void* src, size_t n)` | `n <= dest._capacity`, `n <= src._capacity` |
| `memset` | `void* memset(void* dest, int c, size_t n)` | `n <= dest._capacity` |
| `memcmp` | `int memcmp(const void* s1, const void* s2, size_t n)` | `n <= s1._capacity`, `n <= s2._capacity` |
| `memchr` | `void* memchr(const void* s, int c, size_t n)` | `n <= s._capacity` |

### Tier 2: String Copy Functions (High Priority)

| Function | Signature | Preconditions |
|----------|-----------|---------------|
| `strcpy` | `char* strcpy(char* dest, const char* src)` | `strlen(src) + 1 <= dest._capacity` |
| `strncpy` | `char* strncpy(char* dest, const char* src, size_t n)` | `n <= dest._capacity` |
| `strcat` | `char* strcat(char* dest, const char* src)` | `strlen(dest) + strlen(src) + 1 <= dest._capacity` |
| `strncat` | `char* strncat(char* dest, const char* src, size_t n)` | `strlen(dest) + min(n, strlen(src)) + 1 <= dest._capacity` |

### Tier 3: Format Functions (Medium Priority)

| Function | Signature | Preconditions |
|----------|-----------|---------------|
| `sprintf` | `int sprintf(char* buf, const char* fmt, ...)` | **ALWAYS WARN** (unbounded) |
| `snprintf` | `int snprintf(char* buf, size_t n, const char* fmt, ...)` | `n <= buf._capacity` |
| `vsprintf` | `int vsprintf(char* buf, const char* fmt, va_list)` | **ALWAYS WARN** (unbounded) |
| `vsnprintf` | `int vsnprintf(char* buf, size_t n, const char* fmt, va_list)` | `n <= buf._capacity` |

### Tier 4: Input Functions (Medium Priority)

| Function | Signature | Preconditions |
|----------|-----------|---------------|
| `gets` | `char* gets(char* buf)` | **ALWAYS ERROR** (deprecated, removed in C11) |
| `fgets` | `char* fgets(char* buf, int n, FILE* stream)` | `n <= buf._capacity` |
| `scanf("%s")` | - | **WARN** when used with `%s` without width |
| `fscanf("%s")` | - | **WARN** when used with `%s` without width |

### Tier 5: BSD/Safe Variants (Low Priority - Postconditions)

| Function | Signature | Notes |
|----------|-----------|-------|
| `strlcpy` | `size_t strlcpy(char* dest, const char* src, size_t n)` | `n <= dest._capacity`; returns would-be length |
| `strlcat` | `size_t strlcat(char* dest, const char* src, size_t n)` | `n <= dest._capacity`; returns would-be length |
| `memcpy_s` | `errno_t memcpy_s(void* dest, size_t destsz, const void* src, size_t n)` | `n <= destsz`, `destsz <= dest._capacity` |
| `strcpy_s` | `errno_t strcpy_s(char* dest, size_t destsz, const char* src)` | `destsz <= dest._capacity` |

---

## Ghost State Model

### Buffer Capacity Tracking

```cpp
// Ghost variable: _capacity (in bytes)
// Represents the allocated size of a buffer

// Stack arrays - capacity inferred from declaration
char buf[100];           // buf._capacity = 100
int arr[50];             // arr._capacity = 200 (50 * sizeof(int))
struct S data[10];       // data._capacity = 10 * sizeof(struct S)

// Pointers from arrays - capacity inherited
char* p = buf;           // p._capacity = buf._capacity = 100
char* q = buf + 10;      // q._capacity = buf._capacity - 10 = 90

// Dynamic allocation (future phase)
char* h = malloc(256);   // h._capacity = 256
char* h2 = realloc(h, 512);  // h2._capacity = 512
```

### Capacity Arithmetic

```cpp
// Pointer arithmetic adjusts capacity
char buf[100];           // buf._capacity = 100
char* p = buf + 20;      // p._capacity = 80
char* q = p + 30;        // q._capacity = 50

// Symbolic arithmetic
void f(char* buf, size_t cap, size_t offset) {
    // buf._capacity = cap (from annotation)
    char* p = buf + offset;  // p._capacity = cap - offset

    if (offset < cap) {
        // In this branch: p._capacity > 0
        memset(p, 0, cap - offset);  // OK: cap - offset <= cap - offset
    }
}
```

### String Length Tracking

```cpp
// Ghost variable: _strlen (for null-terminated strings)
// Represents strlen() of the content (not including null terminator)

// String literals - length known at compile time
const char* s = "hello";   // s._strlen = 5, s._capacity = 6

// After strlen() call
char buf[100];
size_t len = strlen(buf);  // Establishes: buf._strlen = len

// After string operations
strcpy(buf, "test");       // buf._strlen = 4
strcat(buf, "!");          // buf._strlen = 5
```

---

## Implementation Design

### Phase 1a: memcpy Family on Stack Arrays

**Scope:** Fixed-size stack arrays with explicit size argument.

```cpp
// annotations/std.dtt additions (shown in old C++ syntax for historical context)

// memcpy(dest, src, n) - requires n <= dest._capacity AND n <= src._capacity
{
    MethodInfo info;

    // Constraint: n <= dest._capacity
    Constraint c1;
    c1.op = Constraint::LE;
    c1.isExprConstraint = true;
    c1.lhsExpr = SymValue::Var("arg2");      // n
    c1.rhsExpr = SymValue::Var("arg0._capacity");
    info.preconditions.push_back(c1);

    // Constraint: n <= src._capacity
    Constraint c2;
    c2.op = Constraint::LE;
    c2.isExprConstraint = true;
    c2.lhsExpr = SymValue::Var("arg2");      // n
    c2.rhsExpr = SymValue::Var("arg1._capacity");
    info.preconditions.push_back(c2);

    MethodAnnotations["memcpy/3"] = info;
}
```

**DTTPlugin.cpp changes:**

```cpp
// In VisitVarDecl or similar - infer capacity from array type
if (auto* AT = dyn_cast<ConstantArrayType>(VD->getType())) {
    uint64_t size = AT->getSize().getZExtValue();
    uint64_t elemSize = Ctx->getTypeSize(AT->getElementType()) / 8;
    setFieldState(varPath, "_capacity", SymValue::Const(size * elemSize));
}
```

### Phase 1b: Pointer Arithmetic Tracking

```cpp
// Track capacity through pointer arithmetic
// p = base + offset  →  p._capacity = base._capacity - offset

bool VisitBinaryOperator(BinaryOperator* BO) {
    if (BO->getOpcode() == BO_Add && BO->getType()->isPointerType()) {
        // Pointer addition: ptr + offset
        std::string ptrPath = getVarPath(BO->getLHS());
        auto baseCap = getFieldState(ptrPath, "_capacity");

        if (baseCap) {
            SymValue offset = evaluateExpr(BO->getRHS());
            SymValue newCap = SymValue::Sub(baseCap->value, offset);
            // Store for result...
        }
    }
}
```

### Phase 1c: String Literal Tracking

```cpp
// String literals have known length and capacity
bool VisitStringLiteral(StringLiteral* SL) {
    size_t len = SL->getLength();  // Not including null terminator
    // _strlen = len
    // _capacity = len + 1
}

// strcpy(dest, "hello") - check: strlen("hello") + 1 <= dest._capacity
//                         i.e.,  6 <= dest._capacity
```

---

## Test Cases (CVE-Inspired)

### Test File: `810_memcpy_pass.cpp`

```cpp
// Variant 1: Basic safe memcpy
void test_basic_safe() {
    char dest[100];
    char src[50];
    memcpy(dest, src, 50);  // OK: 50 <= 100, 50 <= 50
}

// Variant 2: Size from sizeof
void test_sizeof() {
    char dest[100];
    char src[100];
    memcpy(dest, src, sizeof(src));  // OK: 100 <= 100
}

// Variant 3: Guarded copy
void test_guarded(char* src, size_t src_len) {
    char dest[256];
    if (src_len <= sizeof(dest)) {
        memcpy(dest, src, src_len);  // OK: guarded
    }
}

// Variant 4: Pointer arithmetic (CVE-2023-38545 pattern, fixed)
void test_ptr_arith() {
    char buf[100];
    char* p = buf + 10;
    memcpy(p, "data", 5);  // OK: 5 <= 90
}

// Variant 5: Symbolic with proof
void test_symbolic(size_t n) {
    char buf[100];
    if (n <= 100) {
        memcpy(buf, "x", n);  // OK: n <= 100 proven
    }
}
```

### Test File: `811_memcpy_fail.cpp`

```cpp
// Variant 1: Obvious overflow (CVE-2024-24450 pattern)
void test_obvious_overflow() {
    char dest[10];
    char src[100];
    memcpy(dest, src, 100);  // ERROR: 100 > 10
}

// Variant 2: Pointer arithmetic overflow
void test_ptr_overflow() {
    char buf[100];
    char* p = buf + 90;
    memcpy(p, "hello world", 12);  // ERROR: 12 > 10
}

// Variant 3: Unguarded symbolic (Heartbleed pattern)
void test_unguarded(size_t payload_len) {
    char buf[256];
    memcpy(buf, data, payload_len);  // ERROR: can't prove payload_len <= 256
}

// Variant 4: sizeof wrong buffer
void test_wrong_sizeof() {
    char dest[10];
    char src[100];
    memcpy(dest, src, sizeof(src));  // ERROR: 100 > 10
}

// Variant 5: Source too small
void test_src_overflow() {
    char dest[100];
    char src[10];
    memcpy(dest, src, 50);  // ERROR: 50 > src._capacity (10)
}
```

### Test File: `812_strcpy_pass.cpp`

```cpp
// Variant 1: Known-length literal
void test_literal() {
    char buf[20];
    strcpy(buf, "hello");  // OK: 6 <= 20
}

// Variant 2: After strlen check
void test_strlen_guard(const char* src) {
    char buf[100];
    if (strlen(src) < sizeof(buf)) {
        strcpy(buf, src);  // OK: strlen(src) + 1 <= 100
    }
}
```

### Test File: `813_strcpy_fail.cpp`

```cpp
// Variant 1: Literal overflow
void test_literal_overflow() {
    char buf[5];
    strcpy(buf, "hello");  // ERROR: 6 > 5
}

// Variant 2: Unguarded (Moonlight CVE pattern)
void test_unguarded(const char* server_version) {
    char buf[256];
    strcpy(buf, server_version);  // ERROR: can't prove strlen <= 255
}
```

### Test File: `814_sprintf_fail.cpp`

```cpp
// Variant 1: sprintf always warns
void test_sprintf_warn() {
    char buf[100];
    sprintf(buf, "%s", name);  // ERROR/WARN: sprintf is unbounded
}

// Variant 2: Recommend snprintf
void test_use_snprintf() {
    char buf[100];
    snprintf(buf, sizeof(buf), "%s", name);  // OK: bounded
}
```

### Test File: `815_snprintf_pass.cpp`

```cpp
// Variant 1: Correct usage
void test_correct() {
    char buf[100];
    snprintf(buf, sizeof(buf), "value: %d", x);  // OK
}

// Variant 2: Explicit size
void test_explicit() {
    char buf[100];
    snprintf(buf, 100, "value: %d", x);  // OK: 100 <= 100
}
```

### Test File: `816_snprintf_fail.cpp`

```cpp
// Variant 1: Size exceeds buffer (CVE-2018-18820 adjacent)
void test_size_exceeds() {
    char buf[50];
    snprintf(buf, 100, "data");  // ERROR: 100 > 50
}

// Variant 2: Accumulation bug pattern
void test_accumulation() {
    char buf[100];
    size_t offset = 0;
    offset += snprintf(buf, sizeof(buf), "first");
    offset += snprintf(buf + offset, sizeof(buf) - offset, "second");
    // Note: This is actually safe IF snprintf truncates correctly
    // The CVE was about misusing the return value
}
```

---

## Error Messages

### Format

```
error: buffer overflow in memcpy: size (100) exceeds destination capacity (50)
   --> file.cpp:15:5
    |
 12 |     char dest[50];
    |          ---- destination buffer declared here with capacity 50
 ...
 15 |     memcpy(dest, src, 100);
    |     ^^^^^^^^^^^^^^^^^^^^^^ copies 100 bytes
    |
note: memcpy requires: n <= dest._capacity
help: use a smaller size or increase buffer capacity
```

```
warning: strcpy with unverified source length
   --> file.cpp:10:5
    |
  8 |     char buf[256];
    |          --- destination buffer with capacity 256
 ...
 10 |     strcpy(buf, user_input);
    |     ^^^^^^^^^^^^^^^^^^^^^^^ source length unknown
    |
note: strcpy requires: strlen(src) + 1 <= dest._capacity
help: check length first: if (strlen(user_input) < sizeof(buf)) strcpy(...)
help: or use strncpy(buf, user_input, sizeof(buf) - 1)
```

```
error: sprintf is unsafe - use snprintf instead
   --> file.cpp:5:5
    |
  5 |     sprintf(buf, "%s", data);
    |     ^^^^^^^^^^^^^^^^^^^^^^^^
    |
note: sprintf provides no buffer overflow protection (CWE-120)
help: replace with: snprintf(buf, sizeof(buf), "%s", data)
```

---

## Implementation Checklist

### Phase 1a: Core memcpy ✅ COMPLETE

- [x] Add `_capacity` ghost variable infrastructure (`_size_bytes`, `_base_length`)
- [x] Infer capacity for stack arrays in `VisitVarDecl`
- [x] Add `memcpy/3`, `memmove/3`, `memset/3`, `memcmp/3`, `memchr/3` checks
- [x] Create test files 810-811
- [x] Verify tests pass/fail correctly

### Phase 1b: String functions ✅ COMPLETE

- [x] Add string literal length tracking
- [x] Add `strcpy/2`, `strncpy/3`, `strcat/2`, `strncat/3` checks
- [x] Handle source buffer bounds checking
- [x] Create test files 876-879
- [x] Verify tests

### Phase 1c: Format functions ✅ COMPLETE

- [x] Add `sprintf` as unconditional warning/error (CWE-120)
- [x] Add `snprintf/3` bounds checking
- [x] Create test files 876-877
- [x] Verify tests

### Phase 1d: Pointer arithmetic ✅ COMPLETE

- [x] Track capacity through `ptr + offset` via `_offset` ghost variable
- [x] Track capacity through `ptr++`, `ptr += n`
- [x] `getBufferSizeBytes()` accounts for offset to compute remaining capacity
- [x] Add tests for pointer arithmetic patterns (880-881)

### Phase 1e: Polish ✅ COMPLETE

- [x] Refine error messages with CWE references (CWE-120, CWE-126, CWE-787)
- [x] Add diagnostic hints/fixits (NoteUseStrncpy, NoteUseStrncat, NoteCheckSrcLen)
- [x] Documentation updated
- [x] Verified against CVE-inspired test patterns

---

## Future Phases (Out of Scope)

- **Phase 2:** Dynamic allocation (`malloc`, `realloc`, `new[]`)
- **Phase 3:** Cross-field capacity (`data._capacity = length`)
- **Phase 4:** Parameter capacity annotations
- **Phase 5:** Heuristic inference from naming conventions

---

## References

- [CWE-120: Classic Buffer Overflow](https://cwe.mitre.org/data/definitions/120.html)
- [CWE-787: Out-of-bounds Write](https://cwe.mitre.org/data/definitions/787.html)
- [OWASP Buffer Overflow](https://owasp.org/www-community/vulnerabilities/Buffer_Overflow)
- [CVE-2023-38545 curl SOCKS5](https://curl.se/docs/CVE-2023-38545.html)
- [CVE-2018-18820 Icecast snprintf](https://securitylab.github.com/research/cve-2018-18820-snprintf-vulnerability-icecast/)
- [CVE-2022-26937 Windows NFS](https://www.thezdi.com/blog/2022/6/7/cve-2022-26937-microsoft-windows-network-file-system-nlm-portmap-stack-buffer-overflow)
- [Moonlight strcpy vulnerabilities](https://github.com/moonlight-stream/moonlight-common-c/security/advisories/GHSA-f3h8-j898-5h5v)
