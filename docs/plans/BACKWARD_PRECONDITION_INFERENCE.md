# Backward Precondition Inference for Unsafe Uses

## Implementation Status

| Phase | Description | Status | Commits |
|-------|-------------|--------|---------|
| Phase 1 | Emit inferred precondition notes | ✅ Complete | 936293e |
| Phase 1.5 | Store preconditions per function | ✅ Complete | 936293e |
| Phase 2 | Call-site checking (same-TU) | ✅ Complete | fac275f, d4c7ec6, a2538c3 |
| Phase 3 | Pointer arithmetic base tracking | In Progress | - |

**Test Coverage:**
- `911_infer_precondition_symbolic_pass.cpp` - 36 variants for Phase 1/1.5
- `920_callsite_precondition_fail.cpp` - 30 variants for Phase 2 failures
- `921_callsite_precondition_satisfied_pass.cpp` - 25 variants for Phase 2 passes

## Summary

When DTT encounters an unsafe or unproven use (e.g., `memcpy(dst, src, n)`),
infer the missing preconditions by propagating constraints backward through
the current function and emit them as actionable diagnostics. Then, at call
sites within the same translation unit, check these inferred preconditions.

This inverts the current annotation model:
- **Current:** Manual `dtt:requires=` outside → assumed inside function
- **Proposed:** Discovered inside → checked at call sites (auto-annotation)

## Goals

- Turn "cannot verify bounds" into "missing precondition: arg0._size_bytes >= n".
- Automatically infer preconditions from function implementation.
- Check inferred preconditions at call sites (same-TU).
- Avoid field-name heuristics; rely only on dataflow and symbolic rewrites.
- Keep default behavior additive (Category D warnings in `inform`).
- Make `--dtt-mode=enforce` block unproven constraints at API boundaries.

## Non-Goals

- No cross-TU precondition checking in first version (use manual annotations).
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

### Phase 1.5: Store Preconditions Per Function

After Phase 1 emits notes, store the inferred preconditions for later lookup:

```cpp
// In DTTTypes.h
struct InferredPrecondition {
    std::string path;                        // e.g., "buf", "buf->data"
    PreconditionKind kind;                   // NonNull or SizeGE
    std::string boundStr;                    // For SizeGE: string repr of bound
    std::optional<unsigned> argIndex;        // Which parameter the buffer refers to
    std::optional<unsigned> boundArgIndex;   // Which parameter the bound refers to
    int64_t boundConstant = 0;               // Constant part of bound
};

// In DTTPlugin.cpp - per-function storage
llvm::DenseMap<const FunctionDecl*, llvm::SmallVector<InferredPrecondition, 4>> FnPreconditions;
```

**Sources of inferred preconditions:**

1. **Dereference chains** → NonNull preconditions
   - `p->a->b->c` implies: p != null, p->a != null, p->a->b != null

2. **mem* calls** → SizeGE preconditions
   - `memcpy(dst, src, n)` implies: dst._size_bytes >= n, src._size_bytes >= n

3. **Array subscript** → SizeGE preconditions
   - `p[i]` implies: p._size >= i+1

### Phase 2: Call-Site Checking (Same-TU)

At each call site, look up the callee's inferred preconditions and verify them:

```cpp
// In DTTVisitor class (DTTPlugin.cpp)
void checkCallSitePreconditions(CallExpr* E, FunctionDecl* Callee) {
    auto precondIt = FnPreconditions.find(Callee->getCanonicalDecl());
    if (precondIt == FnPreconditions.end()) return;

    for (const auto& Pre : precondIt->second) {
        if (Pre.kind != PreconditionKind::SizeGE) continue;
        if (!Pre.argIndex || *Pre.argIndex >= E->getNumArgs()) continue;

        // Get actual buffer argument and its size at call site
        Expr* ActualBufArg = E->getArg(*Pre.argIndex);
        SymValue bufSize = getBufferSizeAtCallSite(ActualBufArg);

        // Get bound value from actual size argument
        SymValue boundVal = getBoundValueAtCallSite(Pre, E);

        // Check if bufSize >= boundVal is provable via Z3
        if (!canProveBoundsAtCallSite(bufSize, boundVal)) {
            emitCallSitePreconditionWarning(E, Pre, ActualBufArg, Callee);
        }
    }
}
```

**Example:**

```cpp
void inner(char* buf, size_t n) {
    memcpy(buf, "data", n);  // Infers: buf._size_bytes >= n
}

void outer(char* p, size_t sz) {
    inner(p, sz);  // CHECK: is p._size_bytes >= sz provable?
    inner(p, 10);  // CHECK: is p._size_bytes >= 10 provable?
}
```

### Phase 3: Pointer Arithmetic Base Tracking

For `p = base + off; memcpy(p, src, n)`:

**Problem:** DTT copies `_base_length` value but doesn't track that `p` derived from `base`.
Currently emits `p._size_bytes >= n`, but `p` is local - callers pass `base`.

**Solution:** Side table tracking pointer provenance, separate from TypedEnv.

```cpp
// In DTTPlugin.cpp - new state
struct BaseOriginInfo {
    FieldPath basePath;                      // path to original base
    SymValue offsetBytes;                    // accumulated byte offset
    std::optional<unsigned> baseParamIdx;    // if base is a parameter
};

llvm::DenseMap<FieldPath, BaseOriginInfo> PointerBaseOrigins;
```

```cpp
// In DTTTypes.h - extend InferredPrecondition
struct InferredPrecondition {
    // ... existing fields ...
    std::optional<unsigned> offsetArgIndex;  // offset param index
    int64_t offsetConstant = 0;              // constant part of offset (bytes)
};
```

**Update sites:**
1. Pointer arithmetic init (`p = base + off`) → set `PointerBaseOrigins[p]`
2. Pointer copy (`q = p`) → copy `PointerBaseOrigins[p]` to `q`
3. Compound assignment (`p += off`) → accumulate `offsetBytes`
4. Unary ops (`++p`, `p++`) → accumulate `offsetBytes`
5. Reassignment to unrelated pointer → clear entry

**Precondition rewriting:**
When emitting precondition for buffer with known base origin:
- Lookup `PointerBaseOrigins[bufPath]`
- If `baseParamIdx` is set, rewrite: `base._size_bytes >= n + offsetBytes`
- If no parameter base (local array), emit note but skip `FnPreconditions`

**Call-site checking:**
Compute bound as `sizeArg + offsetArg + offsetConstant` using extended fields.

**Design principles:**
- No heuristics - pure dataflow tracking
- Offset in bytes (survives casts)
- Separate from TypedEnv (no SymValue abuse)
- Conservative on unknown (no entry = fall back to current behavior)

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

### The Key Insight: Auto-Annotation

Consider:
```cpp
void f(A* p) {
    int q[100];
    memcpy(q, p->a->b->c->d, 10);
}
```

From this implementation, DTT infers preconditions ON f() itself:
- p != null (used as p->...)
- p->a != null (used as p->a->...)
- p->a->b != null (used as p->a->b->...)
- p->a->b->c != null (used as p->a->b->c->...)
- p->a->b->c->d._size_bytes >= 10 (source of memcpy)

At every call to f(), these are checked/proven:
```cpp
void caller(A* x) {
    f(x);  // WARNING: cannot prove x != null, x->a != null, etc.
}
```

This is the **inverse** of explicit annotation:
- Explicit: Programmer writes requirements, DTT assumes them inside
- Auto-inferred: DTT discovers requirements inside, checks at call sites

## Risks

- Path sensitivity can inflate obligations; start with simple, local paths.
- Aliasing can weaken constraints; prefer typed paths and conservative output.
- Solver load increases if inference emits complex expressions.

## Implementation Effort

| Phase | Scope | Effort | Status |
|-------|-------|--------|--------|
| Phase 1 | Direct parameters, emit notes | **Small** - formatting and diagnostic emission | ✅ Done |
| Phase 1.5 | Store preconditions per function | **Small** - data structure and collection | ✅ Done |
| Phase 2 | Call-site checking (same-TU) | **Medium** - argument mapping and proof checking | ✅ Done |
| Phase 3 | Pointer arithmetic base tracking | **Medium** - side table + offset accumulation | In Progress |
| Tests | All phases | **Small** | ✅ Done |

## Phase 1 Implementation Checklist

- [x] Add `NoteInferredPrecondition` diagnostic ID
- [x] At unverified memcpy: emit `dst._size_bytes >= copySize.str()`
- [x] At unverified memcpy: emit `src._size_bytes >= copySize.str()`
- [x] Same for memset, memcmp, memchr
- [x] Test 910: simple inference works
- [x] Test 912: guarded case emits bounded precondition
- [x] Test 913: branch-sensitive emission

## Phase 1.5 Implementation Checklist

- [x] Define `InferredPrecondition` struct (DTTTypes.h)
- [x] Add `FnPreconditions` map to plugin state (DTTPlugin.cpp)
- [x] At function entry, clear current function's preconditions
- [x] At unverified mem* calls, store SizeGE preconditions (memcpy, memset, memcmp, memchr)
- [x] Emit NoteInferredPrecondition diagnostic
- [ ] At dereference chains, store NonNull preconditions (future)
- [x] Test 911: verify preconditions note emitted for symbolic buffers

## Phase 2 Implementation Checklist

- [x] Add `checkCallSitePreconditions()` function
- [x] Hook into CallExpr visitation
- [x] Map formal parameters (argN) to actual arguments
- [x] For each precondition, check if provable at call site
- [x] Emit diagnostic if not provable
- [x] Test 920: call-site checking for same-TU functions (30 variants)
- [x] Test 921: call-site with satisfied precondition (25 variants)
- [x] Test 922: call-site with unsatisfied precondition (covered by 920)

## Phase 3 Implementation Checklist

- [ ] Add `BaseOriginInfo` struct to DTTPlugin.cpp
- [ ] Add `PointerBaseOrigins` DenseMap to plugin state
- [ ] Extend `InferredPrecondition` with `offsetArgIndex` and `offsetConstant`
- [ ] Update pointer arithmetic init to populate `PointerBaseOrigins`
- [ ] Update pointer copy (`q = p`) to propagate base origin
- [ ] Update compound assignment (`p += off`) to accumulate offset
- [ ] Update unary ops (`++p`) to accumulate offset
- [ ] Clear base origin on reassignment to unrelated pointer
- [ ] Update `emitInferredPrecondition` to rewrite using base origin
- [ ] Update `checkCallSitePreconditions` to handle offset in bound
- [ ] Test 930: simple offset (`p = base + off; memcpy(p, ...)`)
- [ ] Test 931: chained arithmetic (`q = p + b` where `p = base + a`)
- [ ] Test 932: compound assignment (`p = base; p += off`)
- [ ] Test 933: pointer copy with offset (`q = p` where `p = base + off`)
- [ ] Test 934: non-parameter base (local array)
- [ ] Test 935: call-site with offset satisfied
- [ ] Test 936: call-site with offset violated

## Same-TU vs Cross-TU Limitations

### Why Same-TU First

Clang plugins operate per translation unit. The `FnPreconditions` map lives in
memory during compilation and is not persisted. This means:

- **Same-TU:** `inner()` defined before `outer()` in same file → preconditions available
- **Cross-TU:** `inner()` in different .cpp file → preconditions not available

### Declaration Order Matters

Within a TU, functions must be processed in the right order:

```cpp
// Case 1: Definition before use - WORKS
void inner(char* p, size_t n) { memcpy(p, "x", n); }
void outer(char* p) { inner(p, 10); }  // Can check inner's preconditions

// Case 2: Use before definition - MAY NOT WORK
void outer(char* p);  // Forward declaration
void inner(char* p, size_t n) { memcpy(p, "x", n); }
void outer(char* p) { inner(p, 10); }  // inner() already processed, so OK

// Case 3: Headers - DEPENDS
// If inner() is inline in header, works
// If inner() is in separate .cpp, cross-TU problem
```

### Future: Cross-TU Support

Options for cross-TU precondition sharing:

1. **Serialization:** Emit `.dtt` sidecar files with inferred preconditions
2. **LTO/WPA:** Whole-program analysis at link time
3. **Manual annotation:** For cross-TU, use explicit `dtt:requires=` annotations

For now, cross-TU boundaries remain a Category D situation (unverified).
