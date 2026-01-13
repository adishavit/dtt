# DTT Gap Fixes Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend DTT to detect memory safety violations (use-after-free, double-free, mismatched deallocators) and reduce false positives (bitmask indices, loop bounds, call-site analysis).

**Architecture:** Add a PointerLifetimeState tracking system parallel to the existing nullability tracking. Each pointer gets a state machine: Valid → Freed. Track allocation source (malloc/new/new[]) to verify matching deallocation. Extend bounds inference to recognize bitmask patterns and loop induction variables.

**Tech Stack:** Clang AST, Z3 SMT solver (already integrated), C++17

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

## Summary

| Task | Test File | Detection Type |
|------|-----------|----------------|
| 1.1-1.4 | 700, 701, 705 | Lifetime tracking (UAF, double-free, mismatch) |
| 2.1-2.2 | 702 | Allocation size bounds |
| 3.1 | 703 | Integer overflow in sizes |
| 4.1 | 704 | Negative index detection |
| 5.1 | 706 | Bitmask FP reduction |
| 5.2 | 707 | Loop bound FP reduction |
| 5.3 | 708 | Call-site analysis FP reduction |

**Estimated LOC:** ~500-800 lines of new code in DTTPlugin.cpp
