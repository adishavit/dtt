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

## Required Diagnostics

Add a new note to unverified mem* diagnostics:

- `note: inferred precondition: %0`

Where `%0` is a readable constraint, e.g.:
- `arg0._size_bytes >= n`
- `arg1._size_bytes >= n`
- `arg0._size_bytes >= n + off`

This is a note attached to the existing Category D warning/error.

## Concrete Algorithm

### 1) Generate Use-Site Obligations

At mem* use sites, create obligations for unproven bounds:

- `memcpy(dst, src, n)`:
  - `n <= dst._size_bytes`
  - `n <= src._size_bytes`
- `memset(dst, c, n)`:
  - `n <= dst._size_bytes`
- `memcmp(s1, s2, n)`:
  - `n <= s1._size_bytes`
  - `n <= s2._size_bytes`
- `memchr(s, c, n)`:
  - `n <= s._size_bytes`

These are already in the mem* checks around:
- `src/DTTPlugin.cpp:8627` (memcpy/memmove)
- `src/DTTPlugin.cpp:8672` (memset)
- `src/DTTPlugin.cpp:8695` (memcmp)
- `src/DTTPlugin.cpp:8744` (memchr)

### 2) If Unproven, Backward-Propagate

Only when the solver cannot prove the obligation (Category D),
invoke the inference engine on the current function.

### 3) Backward Rewriting Rules (Intraprocedural)

Rewrite the obligation back through statements, simplifying to parameter paths.

#### Assignment
If `x = expr`, replace `x` in the obligation with `expr`.

#### Pointer arithmetic
If `p = base + k`, then:
`p._size_bytes` becomes `base._size_bytes - k * sizeof(T)`.

#### Member access
Keep member paths (no heuristics):
`arg0->buf` becomes `arg0.buf`, then `arg0.buf._size_bytes`.

#### Guarded paths
If use is inside `if (cond) { ... }`, add `cond` as a path condition.
If the block terminates, the negation is available after the block.

### 4) Constraint Normalization

Normalize to a readable precondition form, e.g.:
`n <= arg0._size_bytes - off` becomes `arg0._size_bytes >= n + off`.

### 5) Emit Inferred Preconditions

Emit a note attached to the existing Category D diagnostic:

`note: inferred precondition: arg0._size_bytes >= n`

If multiple obligations exist, emit multiple notes.

## Minimal AST/CFG Design

### Pass Structure

1) Keep the existing `RecursiveASTVisitor` for normal tracking.
2) Add a lightweight backward scan function that:
   - Starts from the use-site expression
   - Walks parent statements within the same function
   - Applies rewrite rules on assignments and pointer arithmetic

### Suggested Helpers (New)

- `ConstraintExprPtr buildObligationForMemFn(...)`
- `ConstraintExprPtr rewriteConstraintBackward(const Stmt* S, ConstraintExprPtr C)`
- `std::vector<ConstraintExprPtr> inferPreconditionsForUse(const Stmt* Use)`
- `std::string prettyPrintConstraint(const ConstraintExprPtr& C)`

### Suggested Data Needed

- Parent map from `ASTContext` (`getParents`).
- Existing symbolic expression helpers (`ConstraintExpr`, `SymValue`).
- Existing guard extraction where possible.

## Integration Points in src/DTTPlugin.cpp

### Add Diagnostics

Add a new note ID near other diagnostics:
- `NoteInferredPrecondition`

### Hook into mem* checks

Within each mem* check:
- If `checkBoundsProven` returns `nullopt`:
  - Call `inferPreconditionsForUse(E)`.
  - Emit note(s) with inferred constraints.

Suggested locations:
- `src/DTTPlugin.cpp:8627` (memcpy/memmove)
- `src/DTTPlugin.cpp:8672` (memset)
- `src/DTTPlugin.cpp:8695` (memcmp)
- `src/DTTPlugin.cpp:8744` (memchr)

## Tests (TDD Naming)

### 1) Simple inference
File: `examples/910_infer_precondition_memcpy_xfail.cpp`

```cpp
#include <cstddef>
#include <cstring>

void copy_unchecked(char* dst, const char* src, size_t n) {
    memcpy(dst, src, n);  // inferred: dst._size_bytes >= n, src._size_bytes >= n
}
```

### 2) Pointer arithmetic
File: `examples/911_infer_precondition_ptr_arith_xfail.cpp`

```cpp
#include <cstddef>
#include <cstring>

void copy_with_offset(char* base, size_t n, size_t off) {
    char* p = base + off;
    memcpy(p, "data", n);  // inferred: base._size_bytes >= n + off
}
```

### 3) Guarded path should not infer
File: `examples/912_infer_precondition_guarded_pass.cpp`

```cpp
#include <cstddef>
#include <cstring>

void guarded(char* dst, const char* src, size_t n) {
    if (n <= 16) {
        memcpy(dst, src, n);  // no inference
    }
}
```

### 4) Branch-sensitive
File: `examples/913_infer_precondition_branch_xfail.cpp`

```cpp
#include <cstddef>
#include <cstring>

void branchy(char* dst, const char* src, size_t n, bool flag) {
    if (flag) {
        memcpy(dst, src, n);  // inferred constraints apply only on this path
    }
}
```

Rename `_xfail` to `_fail` once inference is implemented and stable.

## Rationale

- Heartbleed is a missing precondition bug at the API boundary.
- Backward inference makes the missing proof explicit without requiring
  field-name heuristics or manual annotations.
- Keeping it as a Category D note respects DTT’s additive safety model.

## Risks

- Path sensitivity can inflate obligations; start with simple, local paths.
- Aliasing can weaken constraints; prefer typed paths and conservative output.
- Solver load increases if inference emits complex expressions.

## Implementation Effort

- Diagnostics and hooks: small-medium.
- Backward rewriting engine: medium.
- Tests and stabilization: small-medium.
