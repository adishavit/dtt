# Backward Precondition Inference for Unsafe Uses

## Summary

When DTT encounters an unsafe or unproven use (e.g., `memcpy(dst, src, n)`),
infer the missing preconditions by propagating constraints backward through
the current function and emit them as actionable diagnostics. This preserves
the A–G philosophy: unknown/unproven remains Category D by default.

## Goals

- Turn "cannot verify bounds" into "missing precondition: arg0._size_bytes >= n".
- Avoid field-name heuristics; rely only on dataflow and symbolic rewrites.
- Keep default behavior additive (Category D warnings in `inform`).
- Make `--dtt-mode=enforce` block unproven constraints at API boundaries.

## Non-Goals

- No interprocedural proof solving in the first version.
- No changes to Category F semantics (only proven violations are errors by default).

## Existing Infrastructure (No Changes Needed)

DTT already has the building blocks for Phase 1:

| Component | Location | Purpose |
|-----------|----------|---------|
| `getTypedPath(Expr*)` | DTTPlugin.cpp | Get path to buffer argument |
| `getSizeValue(Expr*)` | ~line 8850 | Get size as SymValue expression tree |
| `SymValue::str()` | line 1081 | Pretty-print expressions: `(n+10)` |
| `buildSymValueFromExpr()` | line 1138 | Convert AST Expr to SymValue |
| `checkBoundsProven()` | line 8865 | Returns nullopt when unverified |
| `_offset` tracking | throughout | Pointer offset as SymValue |
| `_base_length` tracking | throughout | Array size in elements |

## Required Diagnostics

Add a new note to unverified mem* diagnostics:

- `note: inferred precondition: %0`

Where `%0` is a readable constraint, e.g.:
- `dst._size_bytes >= n`
- `src._size_bytes >= n`
- `base._size_bytes >= n + off`

This is a note attached to the existing Category D warning/error.

## Concrete Algorithm

### Phase 1: Direct Parameters (Simple Cases)

For `memcpy(dst, src, n)` where bounds are unproven:

1. Get buffer path: `dstPath = getTypedPath(DstArg)`
2. Get size expression: `copySize = getSizeValue(SizeArg)`
3. Emit: `dstPath._size_bytes >= copySize.str()`

**No backward walking needed for Phase 1.** When dst/src are directly function
parameters and n is a parameter or simple expression of parameters, we can
emit the precondition directly.

### Phase 2: Pointer Arithmetic (Future)

For `p = base + off; memcpy(p, src, n)`:

**Gap:** DTT copies `_base_length` value but doesn't track that `p` derived from `buf`.

**Solution options:**
1. Add `_base_ptr` field storing source pointer path
2. At use site, look up VarDecl::getInit() to find base

For Phase 2, need to:
1. Identify that `p` came from `base + off`
2. Rewrite precondition as `base._size_bytes >= n + off`

### Use-Site Obligations

At mem* use sites, create obligations for unproven bounds:

- `memcpy(dst, src, n)`:
  - `dst._size_bytes >= n`
  - `src._size_bytes >= n`
- `memset(dst, c, n)`:
  - `dst._size_bytes >= n`
- `memcmp(s1, s2, n)`:
  - `s1._size_bytes >= n`
  - `s2._size_bytes >= n`
- `memchr(s, c, n)`:
  - `s._size_bytes >= n`

### Constraint Normalization

Normalize to a readable precondition form, e.g.:
`n <= arg0._size_bytes - off` becomes `arg0._size_bytes >= n + off`.

## Integration Points in src/DTTPlugin.cpp

### Add Diagnostics

Add a new note ID near other diagnostics:
- `NoteInferredPrecondition`

### Hook into mem* checks

Within each mem* check, when `checkBoundsProven` returns `nullopt`:

```cpp
case BoundsResult::UnverifiedSymbolic:
    Diag.Report(E->getBeginLoc(), DiagMemcpySymbolicUnverified.get(GlobalMode));
    // NEW: Emit inferred precondition
    FieldPath dstPath = getTypedPath(DstArg);
    Diag.Report(E->getBeginLoc(), NoteInferredPrecondition)
        << (dstPath.toString() + "._size_bytes >= " + copySize.str());
    break;
```

Hook locations:
- memcpy/memmove (~line 8950)
- memset (~line 9005)
- memcmp (~line 9035)
- memchr (~line 9095)

## Tests (TDD Naming)

### 1) Simple inference
File: `examples/910_infer_precondition_memcpy_pass.cpp`

```cpp
#include <cstddef>
#include <cstring>

void copy_unchecked(char* dst, const char* src, size_t n) {
    memcpy(dst, src, n);  // inferred: dst._size_bytes >= n, src._size_bytes >= n
}
```

### 2) Pointer arithmetic (Phase 2)
File: `examples/911_infer_precondition_ptr_arith_xfail.cpp`

```cpp
#include <cstddef>
#include <cstring>

void copy_with_offset(char* base, size_t n, size_t off) {
    char* p = base + off;
    memcpy(p, "data", n);  // inferred: base._size_bytes >= n + off (Phase 2)
}
```

### 3) Guard bounds n, still need buffer size preconditions
File: `examples/912_infer_precondition_guarded_pass.cpp`

```cpp
#include <cstddef>
#include <cstring>

void guarded(char* dst, const char* src, size_t n) {
    if (n <= 16) {
        memcpy(dst, src, n);  // inferred: dst._size_bytes >= 16, src._size_bytes >= 16
    }
}
```

**Note:** The guard `n <= 16` bounds the copy size, but we still need buffer
size preconditions. The inferred precondition becomes `dst._size_bytes >= 16`
(the upper bound on n), not "no inference".

### 4) Branch-sensitive
File: `examples/913_infer_precondition_branch_pass.cpp`

```cpp
#include <cstddef>
#include <cstring>

void branchy(char* dst, const char* src, size_t n, bool flag) {
    if (flag) {
        memcpy(dst, src, n);  // inferred: dst._size_bytes >= n (on this path)
    }
}
```

## Rationale

- Heartbleed is a missing precondition bug at the API boundary.
- Backward inference makes the missing proof explicit without requiring
  field-name heuristics or manual annotations.
- Keeping it as a Category D note respects DTT's additive safety model.
- **Compositional:** Each function's inferred preconditions become proof
  obligations for callers. This chains upward to API boundaries without
  needing heavyweight interprocedural analysis.

## Risks

- Path sensitivity can inflate obligations; start with simple, local paths.
- Aliasing can weaken constraints; prefer typed paths and conservative output.
- Solver load increases if inference emits complex expressions.

## Implementation Effort

| Phase | Scope | Effort |
|-------|-------|--------|
| Phase 1 | Direct parameters, simple expressions | **Small** - formatting and diagnostic emission |
| Phase 2 | Pointer arithmetic base tracking | **Medium** - need _base_ptr or VarDecl lookup |
| Tests | All phases | **Small** |

## Phase 1 Implementation Checklist

- [ ] Add `NoteInferredPrecondition` diagnostic ID
- [ ] At unverified memcpy: emit `dst._size_bytes >= copySize.str()`
- [ ] At unverified memcpy: emit `src._size_bytes >= copySize.str()`
- [ ] Same for memset, memcmp, memchr
- [ ] Test 910: simple inference works
- [ ] Test 912: guarded case emits bounded precondition
- [ ] Test 913: branch-sensitive emission
