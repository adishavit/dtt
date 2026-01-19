# DTT Auto-Annotation Spec for Heartbleed-Class Bugs

## Implementation Status

| Change | Description | Status | Commit |
|--------|-------------|--------|--------|
| 1 | Source-side unverified bounds | ✅ Complete | 6e601b4 |
| 2 | Remove constant-only gating | Pending | - |
| 3 | Symbolic _size_bytes tracking | Pending | - |
| 4 | Allocator registry | Pending | - |
| 5 | Taint tracking | Pending | - |

## Background
DTT already detects many bounds issues but can miss Heartbleed-style bugs when:
- The destination buffer is sized from an untrusted length, and
- The source buffer is smaller, causing an over-read, and
- The copy length is not provably bounded.

This spec defines five incremental changes that keep DTT's additive-safety
philosophy (Categories A-G) while closing the Heartbleed gap.

## Goals
- Make Heartbleed-style over-reads trigger diagnostics without manual annotations.
- Preserve DTT's category model: only proven violations are Category F by default.
- Allow stricter enforcement in `--dtt-mode=enforce` without changing defaults.

## Non-Goals
- Do not turn DTT into a gatekeeper in `inform` mode.
- Do not require field-name heuristics.

## Change 1: Source-Side Unverified Bounds for memcpy/memmove ✅

**Status:** Implemented in commit 6e601b4

### Behavior
- When `n <= src._size_bytes` cannot be proven, emit an "unverified" diagnostic
  for the source side (Category D), just like the destination side.
- Only emit Category F when `n > src._size_bytes` is provable.

### Rationale
Heartbleed is a source over-read. Today, source unverified checks are
suppressed, which can allow over-reads to pass silently when the destination
looks safe.

### Implementation Notes
- Remove the suppression of source unverified warnings in the memcpy/memmove
  path.
- Optionally add a source-specific note message to reduce confusion.

### Pros (vs current)
- Closes a real detection gap with minimal behavior change.
- Keeps DTT's categories intact.

### Cons (vs current)
- Slightly more warnings in `inform` mode.
- Potential duplicate warnings if destination is also unverified.

### Category Impact
- Unknown source bounds -> Category D (warn in inform, error in enforce).
- Proven over-read -> Category F (error).

## Change 2: Always Attempt Proof for mem* Bounds (Remove Constant-Only Gating)

### Behavior
- Always attempt to prove `n <= buf_size` via Z3 when a tracked size expression
  exists, even if size is symbolic.
- Emit Category D when the solver cannot prove or disprove.

### Rationale
Many real sizes are symbolic (length fields, arithmetic), so constant-only
checks miss meaningful proofs or violations.

### Implementation Notes
- Lift the `dstBytes.kind == CONST` gating in mem* checks.
- Ensure `SymValue::Var` paths are passed into Z3, and results are treated
  as Category D when unknown.

### Pros (vs current)
- Allows proof of guard-based patterns with symbolic sizes.
- Reduces false negatives for non-constant sizes.

### Cons (vs current)
- More solver calls; potential perf cost.
- More Category D warnings in `inform` mode.

### Category Impact
- Unknown solver result -> Category D.
- Proven overflow -> Category F.

## Change 3: Symbolic _size_bytes for Allocations and Pointer Arithmetic

### Behavior
- Track `_size_bytes` using symbolic expressions, not only constants.
- Propagate symbolic sizes through pointer arithmetic:
  `p = base + k` => `p._size_bytes = base._size_bytes - k * elem_size`.

### Rationale
Heartbleed-style code often uses computed sizes; symbolic tracking allows
Z3 to prove or refute bounds instead of falling back to unknown.

### Implementation Notes
- Store allocation sizes as `SymValue` expressions even when non-constant.
- Extend `SymValue::eval` to fold `MUL/DIV` when operands are constants.
- Propagate `_size_bytes` through assignments and pointer arithmetic paths.

### Pros (vs current)
- Enables proof for dynamic-size buffers without manual annotations.
- Reduces Category D by giving the solver real constraints.

### Cons (vs current)
- Increased symbolic state complexity.
- Requires careful constraint simplification to keep solver fast.

### Category Impact
- No category change; improves provability so fewer D cases remain.

## Change 4: Allocator Registry / Annotation Hook for Wrappers

### Behavior
- Recognize allocator wrappers via:
  - A built-in list (minimal), and
  - Optional user-provided registry (plugin args or annotations).
- Example annotation: `[[clang::annotate("dtt:alloc_size_arg=1")]]`.

### Rationale
Real code uses wrapper allocators (e.g., OpenSSL). Without this, `_size_bytes`
remains unknown and copy checks degrade to Category D.

### Implementation Notes
- Extend allocation detection to check a registry for size argument index.
- Add parser for `dtt:alloc_size_arg=N` on function decls.

### Pros (vs current)
- Practical adoption in codebases with custom allocators.
- Keeps logic explicit, no field-name heuristics.

### Cons (vs current)
- Needs config/annotation documentation.
- Does not handle interprocedural allocators unless annotated.

### Category Impact
- More sizes become tracked; fewer Category D outcomes.

## Change 5: Taint Tracking for Untrusted Lengths

### Behavior
- Add taint to length expressions from untrusted sources.
- Taint propagates through arithmetic and assignments.
- Taint is cleared only when a guard proves `len <= cap`.
- In `inform`, unproven tainted bounds warn; in `enforce`, they error.

### Rationale
This mirrors Idris-style proof gating: you cannot use an untrusted length
without establishing a proof.

### Implementation Notes
- Add a taint bit to `SymValue` or a parallel `TaintEnv` keyed by `FieldPath`.
- Define taint sources via:
  - Built-in IO functions (recv/read/SSL_read/etc).
  - `dtt:tainted` annotation on functions or variables.
- Update guard handling to clear taint on proven bounds.

### Pros (vs current)
- Makes "untrusted length" explicit and enforceable.
- Preserves additive safety by keeping D-category behavior in `inform`.

### Cons (vs current)
- Requires careful propagation rules to avoid false positives.
- Needs a maintainable taint source list.

### Category Impact
- Unproven tainted bounds -> Category D by default.
- Proven overflow -> Category F.

## Test Plan

### New Tests (Concrete Code)

#### 1) Heartbleed-like Over-read (Unguarded) ✅
**File:** `examples/885_heartbleed_overread_fail.cpp`
**Status:** Implemented
**Expectation:** Category D in `inform`, error in `enforce` (unless provable overflow -> F).

```cpp
#include <cstddef>
#include <cstring>
#include <cstdlib>

void test_heartbleed_like(size_t claimed_len) {
    unsigned char record[16];
    unsigned char* payload = record + 3;  // 13 bytes remain
    unsigned char* out = static_cast<unsigned char*>(malloc(claimed_len));
    memcpy(out, payload, claimed_len);  // should warn/error: src over-read unproven
}
```

#### 2) Heartbleed-like Over-read (Guarded) ✅
**File:** `examples/886_heartbleed_guarded_pass.cpp`
**Status:** Implemented
**Expectation:** Pass (proof discharges unverified).

```cpp
#include <cstddef>
#include <cstring>
#include <cstdlib>

void test_heartbleed_like_guarded(size_t claimed_len) {
    unsigned char record[16];
    unsigned char* payload = record + 3;
    unsigned char* out = static_cast<unsigned char*>(malloc(claimed_len));
    if (claimed_len <= sizeof(record) - 3) {
        memcpy(out, payload, claimed_len);  // OK: proven <= 13
    }
}
```

#### 3) Source-Side Unverified Only (Regression for Change 1) ✅
**File:** `examples/884_memcpy_src_unverified_fail.cpp`
**Status:** Implemented
**Expectation:** Unverified warning/error for source (not suppressed).

```cpp
#include <cstddef>
#include <cstring>

void test_src_unverified(size_t n) {
    char dst[16];
    char src[16];
    char* p = src + 4;  // remaining 12 bytes
    if (n <= sizeof(dst)) {
        memcpy(dst, p, n);  // dst safe; src should be unverified
    }
}
```

#### 4) Symbolic Allocation Size (Guarded)
**File:** `examples/malloc_symbolic_guarded_pass.cpp`  
**Expectation:** Pass when guarded.

```cpp
#include <cstddef>
#include <cstring>
#include <cstdlib>

void test_malloc_symbolic_guarded(size_t len) {
    char* buf = static_cast<char*>(malloc(len + 3));
    if (len <= len + 3) {
        memcpy(buf, "abc", len);  // OK: symbolic size proven
    }
}
```

#### 5) Symbolic Allocation Size (Unguarded)
**File:** `examples/malloc_symbolic_unguarded_xfail.cpp`  
**Rename to:** `examples/malloc_symbolic_unguarded_fail.cpp` once Change 3 lands.  
**Expectation:** Category D in `inform`, error in `enforce`.

```cpp
#include <cstddef>
#include <cstring>
#include <cstdlib>

void test_malloc_symbolic_unguarded(size_t len) {
    char* buf = static_cast<char*>(malloc(len + 3));
    memcpy(buf, "abc", len + 4);  // unproven, potential overflow
}
```

#### 6) Taint Source Without Guard
**File:** `examples/taint_length_unguarded_xfail.cpp`  
**Rename to:** `examples/taint_length_unguarded_fail.cpp` once Change 5 lands.  
**Expectation:** Category D in `inform`, error in `enforce`.

```cpp
#include <cstddef>
#include <cstring>

// recv() is treated as taint source by the plugin.
extern int recv(int fd, void* buf, size_t len, int flags);

void test_taint_unguarded(int fd) {
    size_t len = 0;
    recv(fd, &len, sizeof(len), 0);  // tainted length
    char buf[32];
    memcpy(buf, "data", len);  // warn/error: tainted, unproven
}
```

#### 7) Taint Source With Guard
**File:** `examples/taint_length_guarded_pass.cpp`  
**Expectation:** Pass.

```cpp
#include <cstddef>
#include <cstring>

extern int recv(int fd, void* buf, size_t len, int flags);

void test_taint_guarded(int fd) {
    size_t len = 0;
    recv(fd, &len, sizeof(len), 0);
    char buf[32];
    if (len <= sizeof(buf)) {
        memcpy(buf, "data", len);  // OK: taint discharged
    }
}
```

### Regression Coverage
- Ensure existing Category C/D behavior in `inform` remains unchanged.
- Ensure `--dtt-mode=enforce` turns D into errors as designed.

## Feasibility and Effort Summary

- Change 1: High feasibility, small-medium effort, no redesign.
- Change 2: High feasibility, medium effort, limited redesign in mem* checks.
- Change 3: Medium feasibility, medium-high effort, requires symbolic tracking
  and Z3 constraint integration.
- Change 4: Medium feasibility, medium effort, adds configuration/annotation path.
- Change 5: High feasibility, medium effort, new taint propagation logic.

## Open Questions
- Should source-side unverified diagnostics be de-duplicated with destination
  warnings to reduce noise?
- What is the minimal taint source set to avoid false positives in practice?
- How should allocator registry be configured (command line vs annotations)?

## Gap-to-Fix Map (Heartbleed-Specific)

| Gap | Status | Effect on Heartbleed | Plan Changes | Expected Diagnostic |
| --- | --- | --- | --- | --- |
| Source unverified check suppressed | ✅ **Fixed** (6e601b4) | Source over-read now detected | Change 1 | Category D in inform, error in enforce |
| Constant-only mem* gating | Pending | Symbolic sizes bypass proof checks | Change 2, Change 3 | Category D if unproven |
| Alloc sizes only tracked for constants | Pending | Wrapper allocs unknown | Change 3, Change 4 | More provable bounds |
| No taint model for untrusted length | Pending | Untrusted length = unknown | Change 5 | Category D unless proven safe |
| Heartbleed test pattern missing | ✅ **Fixed** | Tests added: 884, 885, 886 | Test plan | Deterministic warn/error |
