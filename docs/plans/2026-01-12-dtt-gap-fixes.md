# DTT Gap Fixes Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend DTT to detect memory safety violations (use-after-free, double-free, mismatched deallocators) and reduce false positives (bitmask indices, loop bounds, call-site analysis).

**Architecture:** Add a PointerLifetimeState tracking system parallel to the existing nullability tracking. Each pointer gets a state machine: Valid → Freed. Track allocation source (malloc/new/new[]) to verify matching deallocation. Extend bounds inference to recognize bitmask patterns and loop induction variables.

**Tech Stack:** Clang AST, Z3 SMT solver (already integrated), C++17

---

## Implementation Status

| Phase | Description | Test | Status |
|-------|-------------|------|--------|
| 1.1 | PointerLifetimeState infrastructure | - | ✅ Done (ddda54e) |
| 1.2 | Track allocation sources | - | ✅ Done (ad4d2cd) |
| 1.3 | Detect double-free & mismatch | 701, 705 | ✅ Done (ca58a5f) |
| 1.4 | Detect use-after-free | 700 | ✅ Done (e5268e7) |
| 2 | Heap allocation size tracking | 702 | ✅ Done (7ebadcf) - Simplified |
| 3 | Integer overflow in sizes | 703 | ⏳ Gap - Complex (see analysis below) |
| 4 | Negative index detection | 704 | ✅ Done - Already working via bounds check |
| 5.1 | Bitmask index recognition | 706 | ✅ Done (fc10d52) |
| 5.2 | Loop bound tracking | 707 | 🔶 Partial - 25/30 variants pass |
| 5.3 | Call-site analysis | 708 | ⏳ Gap - Not implemented |
| 6 | Uninitialized pointer tracking | 709 | ✅ Done (7c2dfcf) |

**Phase 2 was simplified:** Instead of adding a separate size field to PointerLifetimeState, we reuse the existing `_size`/`_size_bytes` infrastructure. When malloc/calloc/new[] is called, we extract the size argument and store it via `setField()`. The existing Z3 bounds checker automatically uses it.

**Phase 4 (704) was already working:** DTT's existing bounds checker already catches negative indices via Z3 constraint solving. No new code needed.

**Phase 5.1 (706) implementation:** Added `BITAND` and `MOD` SymValue kinds with `upperBound()` method to recognize safe indexing patterns like `arr[i & 7]` and `arr[i % 10]`.

**Phase 6 (709) implementation:** Added `_initialized` tracking for local pointers. Detects use of uninitialized pointers in comparisons, dereferences, copies, and casts.

---

## Phase 1: Pointer Lifetime Tracking Infrastructure

### Task 1.1: Add PointerLifetimeState Enum and Map

**Files:**
- Modify: `src/DTTPlugin.cpp:60-75` (near TrackingState enum)

**Step 1: Write the failing test**

The test already exists: `examples/700_use_after_free_xfail.cpp` variant 1.
This test should fail (DTT error) but currently passes (no detection).

**Step 2: Verify test currently "passes" (gap exists)**

Run: `cmake --build build && ctest --test-dir build -R 700_use_after_free`
Expected: Test PASSES (meaning DTT doesn't catch it - the gap)

**Step 3: Add lifetime state enum after TrackingState**

```cpp
// Pointer lifetime states for use-after-free / double-free detection
enum class PointerLifetime {
    Valid,      // Pointer to live memory
    Freed,      // Memory has been freed - any use is UAF
    Unknown     // Lifetime not tracked
};

// Allocation source for mismatched dealloc detection
enum class AllocSource {
    Malloc,     // malloc, calloc, realloc
    New,        // new T
    NewArray,   // new T[]
    AlignedAlloc, // aligned_alloc, posix_memalign
    Placement,  // placement new (no dealloc needed)
    Unknown
};

struct PointerLifetimeState {
    PointerLifetime lifetime = PointerLifetime::Unknown;
    AllocSource allocSource = AllocSource::Unknown;
    SourceLocation allocLocation;    // Where allocated
    SourceLocation freeLocation;     // Where freed (if Freed)
};

// Map from pointer path to lifetime state
std::map<std::string, PointerLifetimeState> PointerLifetimes;
```

**Step 4: Commit infrastructure**

```bash
git add src/DTTPlugin.cpp
git commit -m "feat(lifetime): add PointerLifetimeState infrastructure

Adds enum and map for tracking pointer lifetime (Valid/Freed)
and allocation source (malloc/new/new[]/aligned_alloc).

Prepares for use-after-free and double-free detection.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

### Task 1.2: Track Allocation Sources

**Files:**
- Modify: `src/DTTPlugin.cpp` (in VisitCallExpr around line 5320-5340)

**Step 1: Identify allocation detection code**

Search for `isAllocationFunction` or `malloc` handling.
Current code at ~5320 detects malloc/calloc/realloc for null provenance.

**Step 2: Extend to track allocation source**

After detecting allocation, record in PointerLifetimes:

```cpp
// In the block that handles allocation functions
if (isAllocationFunction(CE)) {
    std::string lhsPath = getExprPath(target);
    if (!lhsPath.empty()) {
        PointerLifetimeState pls;
        pls.lifetime = PointerLifetime::Valid;
        pls.allocLocation = CE->getBeginLoc();

        // Determine allocation source
        std::string name = FD->getNameAsString();
        if (name == "malloc" || name == "calloc" || name == "realloc")
            pls.allocSource = AllocSource::Malloc;
        else if (name == "aligned_alloc" || name == "posix_memalign")
            pls.allocSource = AllocSource::AlignedAlloc;

        PointerLifetimes[lhsPath] = pls;
    }
}
```

**Step 3: Handle new expressions**

In VisitCXXNewExpr or where CXXNewExpr is processed:

```cpp
// For new expressions
if (auto *NewE = dyn_cast<CXXNewExpr>(Init)) {
    std::string path = getExprPath(target);
    if (!path.empty()) {
        PointerLifetimeState pls;
        pls.lifetime = PointerLifetime::Valid;
        pls.allocLocation = NewE->getBeginLoc();
        pls.allocSource = NewE->isArray() ? AllocSource::NewArray : AllocSource::New;
        PointerLifetimes[path] = pls;
    }
}
```

**Step 4: Commit**

```bash
git add src/DTTPlugin.cpp
git commit -m "feat(lifetime): track allocation sources (malloc/new/new[])

Records allocation source when pointers are assigned from
allocation functions or new expressions.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

### Task 1.3: Detect Free/Delete and Mark as Freed

**Files:**
- Modify: `src/DTTPlugin.cpp` (in VisitCallExpr)

**Step 1: Add deallocation detection helper**

```cpp
// Check if function is a deallocation function
bool isDeallocationFunction(const FunctionDecl *FD) {
    if (!FD) return false;
    std::string name = FD->getNameAsString();
    return name == "free" || name == "realloc";  // realloc(p, 0) frees
}

// Get expected dealloc for an alloc source
AllocSource expectedDeallocFor(AllocSource src) {
    switch (src) {
        case AllocSource::Malloc:
        case AllocSource::AlignedAlloc:
            return AllocSource::Malloc;  // use free()
        case AllocSource::New:
            return AllocSource::New;     // use delete
        case AllocSource::NewArray:
            return AllocSource::NewArray; // use delete[]
        default:
            return AllocSource::Unknown;
    }
}
```

**Step 2: Track free() calls**

```cpp
// In VisitCallExpr, when free() is called
if (isDeallocationFunction(FD)) {
    if (CE->getNumArgs() >= 1) {
        std::string ptrPath = getExprPath(CE->getArg(0));
        if (!ptrPath.empty()) {
            auto it = PointerLifetimes.find(ptrPath);
            if (it != PointerLifetimes.end()) {
                // Check for double-free
                if (it->second.lifetime == PointerLifetime::Freed) {
                    // EMIT ERROR: double-free detected
                    // Note: it->second.freeLocation has the first free
                }
                // Check for mismatched dealloc
                if (it->second.allocSource != AllocSource::Malloc &&
                    it->second.allocSource != AllocSource::AlignedAlloc) {
                    // EMIT ERROR: free() on non-malloc pointer
                }
                it->second.lifetime = PointerLifetime::Freed;
                it->second.freeLocation = CE->getBeginLoc();
            }
        }
    }
}
```

**Step 3: Handle delete expressions**

In VisitCXXDeleteExpr:

```cpp
bool VisitCXXDeleteExpr(CXXDeleteExpr *DE) {
    std::string ptrPath = getExprPath(DE->getArgument());
    if (ptrPath.empty()) return true;

    auto it = PointerLifetimes.find(ptrPath);
    if (it != PointerLifetimes.end()) {
        // Double-free check
        if (it->second.lifetime == PointerLifetime::Freed) {
            // EMIT ERROR
        }
        // Mismatched check
        bool isArrayDelete = DE->isArrayForm();
        AllocSource expected = isArrayDelete ? AllocSource::NewArray : AllocSource::New;
        if (it->second.allocSource != expected &&
            it->second.allocSource != AllocSource::Unknown) {
            // EMIT ERROR: mismatched new/delete forms
        }
        it->second.lifetime = PointerLifetime::Freed;
        it->second.freeLocation = DE->getBeginLoc();
    }
    return true;
}
```

**Step 4: Run test 701 (double-free)**

Run: `ctest --test-dir build -R 701_double_free`
Expected: Now FAILS (DTT catches double-free - gap fixed!)

**Step 5: Run test 705 (mismatched)**

Run: `ctest --test-dir build -R 705_mismatched`
Expected: Now FAILS (DTT catches mismatch - gap fixed!)

**Step 6: Commit**

```bash
git add src/DTTPlugin.cpp
git commit -m "feat(lifetime): detect double-free and mismatched deallocation

- Tracks when pointers are freed via free()/delete/delete[]
- Emits error on double-free (pointer already freed)
- Emits error on mismatched alloc/dealloc (malloc/delete, new/free, etc.)

Fixes: 701_double_free, 705_mismatched_alloc_dealloc

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

### Task 1.4: Detect Use-After-Free

**Files:**
- Modify: `src/DTTPlugin.cpp` (in VisitDeclRefExpr or pointer use sites)

**Step 1: Add UAF check helper**

```cpp
// Check if accessing a freed pointer
bool checkUseAfterFree(const Expr *E, SourceLocation UseLoc, DiagnosticsEngine &Diag) {
    std::string path = getExprPath(E);
    if (path.empty()) return false;

    auto it = PointerLifetimes.find(path);
    if (it != PointerLifetimes.end() && it->second.lifetime == PointerLifetime::Freed) {
        // EMIT ERROR with notes:
        // - "use of pointer after it was freed"
        // - note: "freed here" at it->second.freeLocation
        // - note: "allocated here" at it->second.allocLocation
        return true;
    }
    return false;
}
```

**Step 2: Call UAF check at dereference sites**

In the code that handles UnaryOperator (dereference), ArraySubscriptExpr, MemberExpr through pointer:

```cpp
// When checking *ptr, ptr[i], ptr->member
bool VisitUnaryOperator(UnaryOperator *UO) {
    if (UO->getOpcode() == UO_Deref) {
        checkUseAfterFree(UO->getSubExpr(), UO->getOperatorLoc(), Diag);
    }
    return true;
}

bool VisitArraySubscriptExpr(ArraySubscriptExpr *ASE) {
    checkUseAfterFree(ASE->getBase(), ASE->getBeginLoc(), Diag);
    return true;
}

bool VisitMemberExpr(MemberExpr *ME) {
    if (ME->isArrow()) {
        checkUseAfterFree(ME->getBase(), ME->getOperatorLoc(), Diag);
    }
    return true;
}
```

**Step 3: Run test 700 (use-after-free)**

Run: `ctest --test-dir build -R 700_use_after_free`
Expected: Now FAILS (DTT catches UAF - gap fixed!)

**Step 4: Rename tests from xfail to fail**

```bash
mv examples/700_use_after_free_xfail.cpp examples/700_use_after_free_fail.cpp
mv examples/701_double_free_xfail.cpp examples/701_double_free_fail.cpp
mv examples/705_mismatched_alloc_dealloc_xfail.cpp examples/705_mismatched_alloc_dealloc_fail.cpp
```

**Step 5: Commit**

```bash
git add src/DTTPlugin.cpp examples/
git commit -m "feat(lifetime): detect use-after-free

Checks pointer lifetime state before dereference, array access,
and arrow member access. Emits error with allocation and free
location notes.

Fixes: 700_use_after_free

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

## Phase 2: Allocation Size Tracking

### Task 2.1: Track Allocation Size with Pointer

**Files:**
- Modify: `src/DTTPlugin.cpp` (extend PointerLifetimeState)

**Step 1: Extend struct to include size**

```cpp
struct PointerLifetimeState {
    PointerLifetime lifetime = PointerLifetime::Unknown;
    AllocSource allocSource = AllocSource::Unknown;
    SourceLocation allocLocation;
    SourceLocation freeLocation;
    std::optional<SymValue> allocSize;  // NEW: size in bytes
    std::optional<int64_t> elementSize; // NEW: sizeof(element type)
};
```

**Step 2: Capture malloc size argument**

```cpp
// When handling malloc(size)
if (name == "malloc") {
    pls.allocSize = evaluateExprToSymValue(CE->getArg(0));
}
// When handling calloc(count, size)
else if (name == "calloc" && CE->getNumArgs() >= 2) {
    auto count = evaluateExprToSymValue(CE->getArg(0));
    auto size = evaluateExprToSymValue(CE->getArg(1));
    pls.allocSize = SymValue::Mul(count, size);
}
// When handling new T[n]
if (NewE->isArray()) {
    pls.allocSize = evaluateExprToSymValue(NewE->getArraySize().value());
    pls.elementSize = getTypeSizeInBytes(NewE->getAllocatedType());
}
```

**Step 3: Commit**

```bash
git add src/DTTPlugin.cpp
git commit -m "feat(bounds): track allocation size with pointers

Records malloc/calloc size argument and new[] array size.
Prepares for buffer overflow detection on heap allocations.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

### Task 2.2: Check Bounds Against Allocation Size

**Files:**
- Modify: `src/DTTPlugin.cpp` (in bounds checking code)

**Step 1: Extend bounds check to use allocation size**

In the existing bounds checking code, add a fallback to allocation size:

```cpp
// When checking ptr[idx], if ptr has tracked allocation size:
auto it = PointerLifetimes.find(ptrPath);
if (it != PointerLifetimes.end() && it->second.allocSize) {
    SymValue maxBytes = *it->second.allocSize;
    SymValue accessOffset = SymValue::Mul(idx, SymValue::Const(elementSize));
    // Check: accessOffset < maxBytes
    if (proveGreaterEqual(accessOffset, maxBytes)) {
        // EMIT ERROR: buffer overflow
    }
}
```

**Step 2: Run test 702**

Run: `ctest --test-dir build -R 702_allocation_size`
Expected: Now FAILS (DTT catches overflow - gap fixed!)

**Step 3: Commit**

```bash
git add src/DTTPlugin.cpp
git commit -m "feat(bounds): check array access against allocation size

Uses tracked allocation size from malloc/calloc/new[] to
detect buffer overflows when accessing heap memory.

Fixes: 702_allocation_size_tracking

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

## Phase 3: Integer Overflow Detection in Sizes

### Task 3.1: Detect Overflow in Size Calculations

**Files:**
- Modify: `src/DTTPlugin.cpp`

**Step 1: Add overflow detection in SymValue arithmetic**

The infrastructure already exists in `safeAdd`, `safeMul`, etc. that return nullopt on overflow.

**Step 2: Emit warnings when size calculations overflow**

```cpp
// When evaluating size expressions for malloc/calloc/new[]
if (auto *BO = dyn_cast<BinaryOperator>(SizeExpr)) {
    if (BO->getOpcode() == BO_Mul || BO->getOpcode() == BO_Add) {
        // Check if operation can overflow
        auto lhs = evaluateExprToSymValue(BO->getLHS());
        auto rhs = evaluateExprToSymValue(BO->getRHS());
        if (lhs.eval() && rhs.eval()) {
            int64_t l = *lhs.eval(), r = *rhs.eval();
            int64_t result;
            bool overflow = (BO->getOpcode() == BO_Mul)
                ? __builtin_mul_overflow(l, r, &result)
                : __builtin_add_overflow(l, r, &result);
            if (overflow) {
                // EMIT ERROR: integer overflow in allocation size
            }
        }
    }
}
```

**Step 3: Handle symbolic overflow with Z3**

For non-constant sizes, use Z3 to check if overflow is possible:

```cpp
// For symbolic sizes, check if overflow is possible under any input
z3::expr sizeExpr = symValueToZ3(allocSize, ctx);
z3::expr maxSize = ctx.bv_val(SIZE_MAX, 64);
solver.add(sizeExpr > maxSize);
if (solver.check() == z3::sat) {
    // EMIT WARNING: allocation size may overflow
}
```

**Step 4: Run test 703**

Run: `ctest --test-dir build -R 703_integer_overflow`
Expected: Now FAILS (DTT catches overflow - gap fixed!)

**Step 5: Commit**

```bash
git add src/DTTPlugin.cpp
git commit -m "feat(overflow): detect integer overflow in allocation sizes

Checks multiplication and addition in malloc/calloc/new[]
size arguments for potential integer overflow.

Fixes: 703_integer_overflow_size

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

## Phase 4: Negative Index Detection

### Task 4.1: Track Signed Index Expressions

**Files:**
- Modify: `src/DTTPlugin.cpp` (bounds checking)

**Step 1: Detect negative index possibility**

```cpp
// In array subscript checking
bool VisitArraySubscriptExpr(ArraySubscriptExpr *ASE) {
    Expr *Idx = ASE->getIdx();
    QualType IdxType = Idx->getType();

    // If index is signed type, check for negative
    if (IdxType->isSignedIntegerType()) {
        SymValue idxVal = evaluateExprToSymValue(Idx);

        // If constant and negative
        if (auto val = idxVal.eval()) {
            if (*val < 0) {
                // EMIT ERROR: negative array index
            }
        }
        // If symbolic, use Z3 to check if can be negative
        else {
            z3::expr idxExpr = symValueToZ3(idxVal, ctx);
            solver.add(idxExpr < 0);
            if (solver.check() == z3::sat) {
                // EMIT WARNING: index may be negative
            }
        }
    }
    return true;
}
```

**Step 2: Handle size_t underflow patterns**

```cpp
// Detect size_t - constant patterns that underflow
if (auto *BO = dyn_cast<BinaryOperator>(Idx)) {
    if (BO->getOpcode() == BO_Sub) {
        QualType lhsType = BO->getLHS()->getType();
        // If unsigned subtraction, check for underflow
        if (lhsType->isUnsignedIntegerType()) {
            auto lhs = evaluateExprToSymValue(BO->getLHS());
            auto rhs = evaluateExprToSymValue(BO->getRHS());
            // Check if rhs > lhs is possible (underflow)
        }
    }
}
```

**Step 3: Run test 704**

Run: `ctest --test-dir build -R 704_negative_index`
Expected: Now FAILS (DTT catches negative index - gap fixed!)

**Step 4: Commit**

```bash
git add src/DTTPlugin.cpp
git commit -m "feat(bounds): detect negative array indices

Checks for:
- Signed indices that are or may be negative
- Unsigned subtraction that may underflow
- size_t - constant patterns

Fixes: 704_negative_index

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

## Phase 5: False Positive Reduction

### Task 5.1: Recognize Bitmask Index Patterns

**Files:**
- Modify: `src/DTTPlugin.cpp` (bounds checking)

**Step 1: Detect bitmask patterns**

```cpp
// Helper to detect bitmask patterns in index expressions
std::optional<int64_t> getBitmaskUpperBound(const Expr *E) {
    if (auto *BO = dyn_cast<BinaryOperator>(E->IgnoreParenImpCasts())) {
        if (BO->getOpcode() == BO_And) {
            // Check for: expr & constant_mask
            if (auto *Lit = dyn_cast<IntegerLiteral>(BO->getRHS()->IgnoreParenImpCasts())) {
                int64_t mask = Lit->getValue().getExtValue();
                // Result is at most mask (e.g., x & 7 is 0-7)
                return mask;
            }
            if (auto *Lit = dyn_cast<IntegerLiteral>(BO->getLHS()->IgnoreParenImpCasts())) {
                int64_t mask = Lit->getValue().getExtValue();
                return mask;
            }
        }
        if (BO->getOpcode() == BO_Rem) {
            // Check for: expr % constant
            if (auto *Lit = dyn_cast<IntegerLiteral>(BO->getRHS()->IgnoreParenImpCasts())) {
                int64_t mod = Lit->getValue().getExtValue();
                // Result is 0 to mod-1
                return mod - 1;
            }
        }
    }
    return std::nullopt;
}
```

**Step 2: Use in bounds checking**

```cpp
// When checking arr[idx]:
if (auto maxIdx = getBitmaskUpperBound(Idx)) {
    // Index is bounded by mask/modulo
    // Compare maxIdx against array size instead of symbolic idx
    if (*maxIdx < arraySize) {
        // SAFE: index cannot exceed array bounds
        return;  // No warning
    }
}
```

**Step 3: Run test 706**

Run: `ctest --test-dir build -R 706_bitmask`
Expected: PASSES (no false positives on bitmask patterns)

**Step 4: Commit**

```bash
git add src/DTTPlugin.cpp
git commit -m "feat(bounds): recognize bitmask index patterns

Detects (expr & mask) and (expr % N) patterns where the
result is bounded. Uses the mask value as upper bound
instead of treating index as unbounded.

Reduces false positives on ring buffer and hash table code.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

### Task 5.2: Track Loop Induction Variables

**Files:**
- Modify: `src/DTTPlugin.cpp`

**Step 1: Add loop context tracking**

```cpp
struct LoopContext {
    const VarDecl *inductionVar;
    SymValue lowerBound;
    SymValue upperBound;  // exclusive
    bool isIncrementing;
};

std::vector<LoopContext> LoopStack;
```

**Step 2: Parse for-loop bounds**

```cpp
bool VisitForStmt(ForStmt *FS) {
    LoopContext ctx;

    // Parse: for (int i = 0; i < N; i++)
    if (auto *Init = dyn_cast_or_null<DeclStmt>(FS->getInit())) {
        if (Init->isSingleDecl()) {
            if (auto *VD = dyn_cast<VarDecl>(Init->getSingleDecl())) {
                ctx.inductionVar = VD;
                if (auto *InitExpr = VD->getInit()) {
                    ctx.lowerBound = evaluateExprToSymValue(InitExpr);
                }
            }
        }
    }

    // Parse condition: i < N
    if (auto *Cond = dyn_cast_or_null<BinaryOperator>(FS->getCond())) {
        if (Cond->getOpcode() == BO_LT || Cond->getOpcode() == BO_LE) {
            ctx.upperBound = evaluateExprToSymValue(Cond->getRHS());
            ctx.isIncrementing = true;
        }
    }

    LoopStack.push_back(ctx);
    return true;
}
```

**Step 3: Use loop bounds in array checking**

```cpp
// When checking arr[i] and i is a loop induction variable:
for (const auto& loop : LoopStack) {
    if (/* idx references loop.inductionVar */) {
        // Index is bounded by loop condition
        // Check loop.upperBound against array size
        if (proveLE(loop.upperBound, arraySize)) {
            // SAFE: loop bound <= array size
            return;
        }
    }
}
```

**Step 4: Run test 707**

Run: `ctest --test-dir build -R 707_loop_bound`
Expected: PASSES (no false positives on bounded loops)

**Step 5: Commit**

```bash
git add src/DTTPlugin.cpp
git commit -m "feat(bounds): track loop induction variable bounds

Parses for-loop structure to extract induction variable
and upper bound. Uses bound in array access checking
to eliminate false positives on bounded loop iteration.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

### Task 5.3: Call-Site Analysis for Static Function Parameters

**Files:**
- Modify: `src/DTTPlugin.cpp`

**Step 1: Collect call sites for static functions**

```cpp
// Map from static function -> list of call sites
std::map<const FunctionDecl*, std::vector<const CallExpr*>> StaticFunctionCallSites;

// In first pass (AnalysisCollector):
bool VisitCallExpr(CallExpr *CE) {
    if (auto *FD = CE->getDirectCallee()) {
        if (FD->isStatic() || !FD->isExternallyVisible()) {
            StaticFunctionCallSites[FD].push_back(CE);
        }
    }
    return true;
}
```

**Step 2: Analyze parameter nullability across all call sites**

```cpp
// For each static function parameter:
bool canParamBeNull(const FunctionDecl *FD, unsigned paramIdx) {
    auto it = StaticFunctionCallSites.find(FD);
    if (it == StaticFunctionCallSites.end()) return true;  // No calls found

    for (const CallExpr *CE : it->second) {
        if (paramIdx < CE->getNumArgs()) {
            Expr *Arg = CE->getArg(paramIdx);
            auto nullability = inferNonnullFromExpr(Arg);
            if (!nullability || *nullability == 0) {
                // This call site may pass null
                return true;
            }
        }
    }
    return false;  // All call sites pass non-null
}
```

**Step 3: Report at specific bad call sites**

```cpp
// When a null dereference of parameter is detected:
// Instead of warning at function definition, warn at bad call sites
if (FD->isStatic() && !canParamBeNull(FD, paramIdx)) {
    // All callers pass non-null - no warning needed
} else {
    // Find which call sites are bad
    for (const CallExpr *CE : StaticFunctionCallSites[FD]) {
        Expr *Arg = CE->getArg(paramIdx);
        auto nullability = inferNonnullFromExpr(Arg);
        if (!nullability || *nullability == 0) {
            // EMIT ERROR at CE->getBeginLoc():
            // "passing potentially null value to function that dereferences it"
        }
    }
}
```

**Step 4: Run test 708**

Run: `ctest --test-dir build -R 708_static_function`
Expected: PASSES (warnings only at bad call sites)

**Step 5: Commit**

```bash
git add src/DTTPlugin.cpp
git commit -m "feat(nullability): call-site analysis for static functions

For static/internal functions, analyzes all call sites to
determine which callers pass potentially-null values.
Reports at specific bad call sites instead of blanket
warning at function definition.

Reduces false positives when most callers pass non-null.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

## Phase 6: Final Integration

### Task 6.1: Run Full Test Suite

**Step 1: Build and run all tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: All tests pass

**Step 2: Verify renamed xfail tests**

The following tests should now be `*_fail.cpp` (DTT catches them):
- 700_use_after_free_fail.cpp
- 701_double_free_fail.cpp
- 702_allocation_size_tracking_fail.cpp
- 703_integer_overflow_size_fail.cpp
- 704_negative_index_fail.cpp
- 705_mismatched_alloc_dealloc_fail.cpp

**Step 3: Verify pass tests still pass**

- 706_bitmask_index_pass.cpp - no false positives
- 707_loop_bound_tracking_pass.cpp - no false positives
- 708_static_function_params_pass.cpp - no false positives

---

### Task 6.2: Update Documentation

**Files:**
- Modify: `README.md`
- Modify: `BLOG.md`

**Step 1: Add new features to README**

Document:
- Use-after-free detection
- Double-free detection
- Mismatched allocator detection
- Allocation size tracking
- Integer overflow detection
- Negative index detection
- Bitmask pattern recognition
- Loop bound tracking
- Call-site analysis

**Step 2: Commit**

```bash
git add README.md BLOG.md
git commit -m "docs: document new lifetime and bounds features

- Use-after-free, double-free, mismatched dealloc detection
- Allocation size tracking for heap bounds
- Integer overflow detection in sizes
- Negative index detection
- FP reduction: bitmask patterns, loop bounds, call-site analysis

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

## Phase 6: Uninitialized Pointer Tracking

### Task 6.1: Track Initialization State for Pointers

**Problem:** DTT treats uninitialized pointers as "nullability unknown", which means null-guards silence warnings. But comparing/using an uninitialized pointer is UB regardless of value:

```cpp
int* p;              // Uninitialized - indeterminate value
if (p != nullptr) {  // UB: comparing uninitialized pointer
    *p = 42;         // DTT accepts (guard satisfied) - but still UB!
}
```

**Files:**
- Modify: `src/DTTPlugin.cpp`

**Step 1: Add Uninitialized state**

Extend nullability tracking to distinguish uninitialized from unknown:

```cpp
// In FieldState or as new field
enum class InitState {
    Initialized,    // Has a defined value
    Uninitialized,  // Never assigned - any use is UB
    Unknown         // May or may not be initialized (e.g., after branches)
};

// Or simpler: add _initialized field alongside _nonnull
// setField(var, "_initialized", SymValue::Const(0));  // false = uninitialized
```

**Step 2: Track in VisitVarDecl**

```cpp
// Pointer without initializer
if (varType->isPointerType() && !D->hasInit()) {
    setField(var, "_initialized", SymValue::Const(0));  // Uninitialized
    setField(var, "_nonnull", SymValue::Unknown());
}
// Pointer with initializer
if (varType->isPointerType() && D->hasInit()) {
    setField(var, "_initialized", SymValue::Const(1));  // Initialized
    // ... existing nullability tracking
}
```

**Step 3: Track in assignments**

```cpp
// Any assignment initializes the pointer
setField(lhsPath, "_initialized", SymValue::Const(1));
```

**Step 4: Check before null-guard acceptance**

Before accepting a null-guard as proof, check initialization:

```cpp
// In guard processing (where `if (p)` or `if (p != nullptr)` adds proof)
SymValue init = getField(ptrPath, "_initialized");
if (init.kind == SymValue::CONST && init.constVal == 0) {
    // EMIT ERROR: "comparing uninitialized pointer is undefined behavior"
    // Don't add the proof - the comparison itself is UB
    return;
}
// Otherwise proceed with normal guard processing
```

**Step 5: Add diagnostic**

```cpp
unsigned ErrUninitializedPointer;
// In constructor:
ErrUninitializedPointer = Diag.getCustomDiagID(DiagnosticsEngine::Error,
    "DTT: use of uninitialized pointer '%0' is undefined behavior");
```

**Step 6: Run test 709**

Run: `ctest --test-dir build -R 709_uninitialized`
Expected: Now FAILS (DTT catches uninitialized pointer use)

**Step 7: Rename test**

```bash
mv examples/709_uninitialized_pointer_xfail.cpp examples/709_uninitialized_pointer_fail.cpp
```

**Step 8: Commit**

```bash
git add src/DTTPlugin.cpp examples/
git commit -m "feat(init): track uninitialized pointer state

Distinguishes uninitialized pointers from unknown-value pointers.
Comparing or using an uninitialized pointer is UB per C++ standard,
even with null guards.

- Tracks _initialized field for pointers
- Emits error when uninitialized pointer is compared or used
- Prevents null-guards from silencing UB

Fixes: 709_uninitialized_pointer

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"
```

---

## Summary

| Task | Test File | Detection Type |
|------|-----------|----------------|
| 1.1-1.4 | 700, 701, 705 | Lifetime tracking (UAF, double-free, mismatch) |
| 2 | 702 | Allocation size bounds |
| 3.1 | 703 | Integer overflow in sizes |
| 4.1 | 704 | Negative index detection |
| 5.1 | 706 | Bitmask FP reduction |
| 5.2 | 707 | Loop bound FP reduction |
| 5.3 | 708 | Call-site analysis FP reduction |
| 6.1 | 709 | Uninitialized pointer tracking |

**Estimated LOC:** ~600-900 lines of new code in DTTPlugin.cpp

---

## Remaining Gaps Analysis

This section documents gaps that remain unimplemented with detailed variant analysis.

### Gap 1: Integer Overflow in Sizes (703)

**Status:** Not implemented - requires complex Z3 bitvector arithmetic

**Problem:** When computing allocation sizes like `count * sizeof(T)`, integer overflow can occur if `count` is very large. This results in undersized allocations and subsequent buffer overflows.

**Why it's complex:**
1. Requires tracking which expressions flow into allocation sizes
2. Need Z3 bitvector arithmetic (not just integer arithmetic) to model overflow
3. Must recognize safe patterns like `if (count > SIZE_MAX / elem_size) return;`
4. Signed vs unsigned overflow have different semantics

**Failing Variants in 703_integer_overflow_size_xfail.cpp:**

| Variant | Pattern | Why Hard |
|---------|---------|----------|
| 1 | `count * sizeof(int)` | Requires tracking external `count` can be huge |
| 2 | `len + 1` | Overflow when `len == SIZE_MAX` |
| 3 | `sizeof(struct) + data_len` | Header + data overflow |
| 4 | `rows * cols * sizeof(int)` | Compound multiplication overflow |
| 5 | Signed `count * sizeof(int)` | Signed overflow is UB, result could be negative |
| 6 | `nmemb * size` (calloc-style) | Two external values multiplied |
| 7 | `current_size * 2` | Realloc growth overflow |
| 8 | `count << 4` | Shift-based multiplication overflow |
| 9 | `(size + 15) & ~15` | Alignment rounding overflow |
| 10 | `count * sizeof(Element)` | Large struct array overflow |
| 11 | `len1 + len2 + 1` | String concatenation overflow |
| 12 | `(uint32_t)width * height` | 32-bit intermediate overflow |
| 14 | `rounded *= 2` in loop | Power-of-2 rounding overflow |
| 15 | `width * channels * height` | Image processing triple multiply |

**Additional Variant Patterns Not Yet Tested:**

```cpp
// Variant 16: Overflow in flexible array member
struct FlexArray { int count; int data[]; };
void variant16_flexible_array(size_t n) {
    size_t size = sizeof(FlexArray) + n * sizeof(int);  // Can overflow
    FlexArray* fa = (FlexArray*)malloc(size);
}

// Variant 17: Overflow in page-aligned allocation
void variant17_page_align(size_t size) {
    size_t pages = (size + 4095) / 4096;  // Addition can overflow
    size_t aligned = pages * 4096;         // Multiplication can overflow
    void* p = aligned_alloc(4096, aligned);
}

// Variant 18: Overflow via macro
#define ARRAY_SIZE(n, type) ((n) * sizeof(type))
void variant18_macro_overflow(size_t n) {
    void* p = malloc(ARRAY_SIZE(n, double));  // Macro hides overflow
}

// Variant 19: Overflow in 2D array allocation
void variant19_2d_array(size_t rows, size_t cols) {
    int** arr = (int**)malloc(rows * sizeof(int*));
    for (size_t i = 0; i < rows; i++)
        arr[i] = (int*)malloc(cols * sizeof(int));  // Each row can overflow
}

// Variant 20: Overflow in VLA-like pattern
void variant20_vla_pattern(int n) {
    if (n < 0) return;
    size_t size = (size_t)n * sizeof(int);  // Cast doesn't prevent overflow
    int* arr = (int*)malloc(size);
}
```

**Implementation Approach (Future):**
1. Track allocation size expressions symbolically
2. Use Z3 bitvector theory to check for overflow
3. Recognize safe patterns (bounds checks before allocation)
4. Add `--dtt-check-overflow` flag to enable (may have FPs)

---

### Gap 2: Loop Bound Tracking (707)

**Status:** Partial - 25/30 variants pass, 5 fail

**Problem:** DTT should recognize loop invariants and not warn about array access within proven loop bounds.

**Failing Variants Analysis:**

| Variant | Line | Pattern | Why It Fails |
|---------|------|---------|--------------|
| 4 | 42 | `constexpr int SIZE = 50; for(i<SIZE)` | DTT doesn't propagate `constexpr` to loop bound |
| 24 | 257 | `for(i<=strlen(src))` | DTT doesn't know `strlen` returns < buffer size |
| 25 | 269 | `for(i<my_min(n,100))` | DTT doesn't track `min` function result |
| 29 | 310 | `arr[i+1]` with `i+=2` step | Step>1 arithmetic not proven safe |

**Additional Variant Patterns to Test:**

```cpp
// Variant 31: Template size parameter
template<size_t N>
void variant31_template_size() {
    int arr[N];
    for (size_t i = 0; i < N; i++) {
        arr[i] = i;  // Should be safe - N is compile-time constant
    }
}

// Variant 32: Static array size deduction
void variant32_static_deduction() {
    int arr[] = {1, 2, 3, 4, 5};  // Size deduced as 5
    for (int i = 0; i < 5; i++) {
        arr[i] *= 2;  // Should DTT know arr has 5 elements?
    }
}

// Variant 33: Loop with std::min
#include <algorithm>
void variant33_std_min(size_t n) {
    int arr[100];
    size_t limit = std::min(n, (size_t)100);
    for (size_t i = 0; i < limit; i++) {
        arr[i] = i;  // Safe: limit <= 100
    }
}

// Variant 34: Loop with ternary bound
void variant34_ternary_bound(size_t n) {
    int arr[50];
    size_t bound = (n > 50) ? 50 : n;
    for (size_t i = 0; i < bound; i++) {
        arr[i] = i;  // Safe: bound <= 50
    }
}

// Variant 35: Loop with external constant
extern const size_t GLOBAL_SIZE;  // Defined elsewhere as 100
void variant35_extern_const() {
    int arr[100];
    for (size_t i = 0; i < GLOBAL_SIZE; i++) {
        arr[i] = i;  // DTT can't know GLOBAL_SIZE == 100
    }
}

// Variant 36: Reverse iteration with size_t (tricky!)
void variant36_reverse_size_t() {
    int arr[10];
    for (size_t i = 9; i != (size_t)-1; i--) {
        arr[i] = i;  // Tricky: relies on wraparound detection
    }
}

// Variant 37: Loop with multiple arrays, different bounds
void variant37_multi_array() {
    int arr1[10], arr2[20];
    for (int i = 0; i < 10; i++) {
        arr1[i] = i;      // Safe: i < 10
        arr2[i] = i * 2;  // Safe: i < 10 < 20
        arr2[i+10] = i;   // Safe: i+10 < 20
    }
}

// Variant 38: Loop with stride and offset
void variant38_stride_offset() {
    int arr[100];
    for (int i = 0; i < 25; i++) {
        arr[i*4] = i;      // Safe: i*4 < 100 when i < 25
        arr[i*4+1] = i;    // Safe: i*4+1 < 100
        arr[i*4+2] = i;    // Safe: i*4+2 < 100
        arr[i*4+3] = i;    // Safe: i*4+3 < 100
    }
}

// Variant 39: Loop bound from array member
struct Container { int data[50]; int size; };
void variant39_member_bound(Container& c) {
    for (int i = 0; i < c.size; i++) {
        c.data[i] = i;  // NOT safe without knowing c.size <= 50
    }
}

// Variant 40: Loop with early termination guarantee
void variant40_early_term() {
    int arr[10] = {1,2,3,4,5,6,7,8,9,10};
    for (int i = 0; ; i++) {  // Infinite loop?
        if (arr[i] == 5) break;  // Terminates when finding 5
        arr[i] = 0;  // Is this safe? Only if 5 exists in arr[0..9]
    }
}
```

**Implementation Improvements Needed:**
1. Track `constexpr` values as concrete constants in loop bounds
2. Add annotations for library functions (`strlen`, `std::min`)
3. Improve arithmetic expression tracking (`i*4+offset`)
4. Add VarBoundProof for computed bounds

---

### Gap 3: Call-site Analysis (708)

**Status:** Not implemented

**Problem:** DTT should use call-site information to reduce false positives. When a function parameter is known at the call site (e.g., pointer is non-null, size is within bounds), DTT should propagate that knowledge.

**Example Patterns from 708_static_function_params_pass.cpp:**

```cpp
// Variant 1: Function with size parameter matches array
void process_array(int* arr, size_t size) {
    for (size_t i = 0; i < size; i++) {
        arr[i] = i;  // FP: DTT doesn't know arr has `size` elements
    }
}

void caller() {
    int data[100];
    process_array(data, 100);  // Call site: arr=data (100 elems), size=100
}

// Variant 2: Function with known non-null pointer
void use_pointer(int* p) {
    *p = 42;  // FP: DTT doesn't know p is non-null from call site
}

void caller2() {
    int x = 0;
    use_pointer(&x);  // Call site: p = &x, which is always non-null
}

// Variant 3: Function with bounds relationship
void copy_buffer(char* dst, const char* src, size_t dst_size, size_t src_len) {
    for (size_t i = 0; i <= src_len; i++) {
        dst[i] = src[i];  // FP: DTT doesn't know dst_size > src_len
    }
}

void caller3() {
    char dest[100];
    char src[] = "hello";
    copy_buffer(dest, src, 100, 5);  // 100 > 5, so safe
}
```

**Additional Variant Patterns:**

```cpp
// Variant 4: Recursive function with decreasing bound
void recursive_process(int* arr, size_t n) {
    if (n == 0) return;
    arr[n-1] = n;  // Is this safe?
    recursive_process(arr, n-1);
}

// Variant 5: Function pointer with known implementation
typedef void (*Processor)(int*, size_t);
void known_impl(int* arr, size_t n) {
    for (size_t i = 0; i < n; i++) arr[i] = 0;
}

void caller5() {
    int data[10];
    Processor p = known_impl;
    p(data, 10);  // Call through function pointer
}

// Variant 6: Virtual method with known dynamic type
struct Base {
    virtual void process(int* arr, size_t n) = 0;
};

struct Derived : Base {
    void process(int* arr, size_t n) override {
        for (size_t i = 0; i < n; i++) arr[i] = i;
    }
};

void caller6() {
    Derived d;
    int data[10];
    d.process(data, 10);  // DTT should know actual type
}

// Variant 7: Lambda with captured size
void variant7_lambda_capture() {
    int arr[20];
    size_t size = 20;
    auto f = [&arr, size]() {
        for (size_t i = 0; i < size; i++) {
            arr[i] = i;  // Safe: captured size == array size
        }
    };
    f();
}

// Variant 8: Template function instantiation
template<size_t N>
void fill_array(int (&arr)[N]) {
    for (size_t i = 0; i < N; i++) {
        arr[i] = i;  // Safe: N is the actual array size
    }
}

void caller8() {
    int data[15];
    fill_array(data);  // N deduced as 15
}

// Variant 9: Assert-based precondition
void with_assert(int* arr, size_t size, size_t idx) {
    assert(idx < size);  // Precondition
    arr[idx] = 42;  // Should be safe after assert
}

// Variant 10: Contract-style annotation (C++20 contracts future)
// [[expects: idx < size]]
void with_contract(int* arr, size_t size, size_t idx) {
    arr[idx] = 42;  // Safe per contract
}
```

**Implementation Approach (Future):**
1. Inter-procedural analysis: track call sites and propagate known values
2. Summary-based analysis: compute function summaries (requires param X, ensures Y)
3. Annotation system: `[[dtt::requires(size <= N)]]` for user hints
4. Context-sensitive analysis: analyze function in context of each call site

---

### Gap 4: Additional Discovered Issues

**Issue: Double-free detection crosses function boundaries**

During 703 testing, DTT incorrectly reported a double-free between different functions:
```
error: DTT: double-free of 'buf' (pointer was already freed)
  104 |         free(buf);   // In variant7
note: first freed here
   33 |     free(buf);       // In variant2 (DIFFERENT FUNCTION!)
```

This indicates the `PointerLifetimes` map is not properly scoped per function.

**Fix needed:** Clear or scope `PointerLifetimes` map at function boundaries.

---

### Summary of Remaining Work

| Gap | Complexity | Estimated Effort | Priority |
|-----|------------|------------------|----------|
| Integer overflow (703) | High | 2-3 days | Medium |
| Loop bound edge cases (707) | Medium | 1 day | High |
| Call-site analysis (708) | Very High | 1-2 weeks | Low |
| Function scope for lifetimes | Low | 2 hours | High |

**Recommended order:**
1. Fix function scope bug (quick win)
2. Improve loop bound tracking for constexpr and computed bounds
3. Integer overflow detection (if resources allow)
4. Call-site analysis (major feature, defer)
