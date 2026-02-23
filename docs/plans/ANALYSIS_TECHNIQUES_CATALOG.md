# Loop Analysis & Path Sensitivity Implementation Plan

This document details the implementation plan for two advanced analysis features:
- **#3: Widening/Narrowing for Loops** - Abstract interpretation for loop invariants
- **#5: Incorrectness Logic** - Path-sensitive analysis for definite bug detection

---

## Review Assessment (2026-02-23)

This section records a deep review of this document against what DTT actually implements today (~16K lines in `DTTPlugin.cpp`), to separate actionable work from aspirational research.

### What DTT Actually Is

A **forward-driven, single-pass AST visitor** (`RecursiveASTVisitor`) with:
- Structured proof stores (`ProofStore` with `DenseMap<FieldPath, SmallDenseSet<Proof>>`)
- Z3 SMT solver for hard constraint queries
- Fixpoint loops for transitive precondition and frame-effect propagation
- Ghost-field resource tracking (`@open`, `@allocated`, `@fd`)
- Declarative `.dtt` annotation files for STL, libc, and Win32

It does **not** have: a CFG, a worklist algorithm, backward analysis, a heap model, separation logic, or points-to analysis. Every technique in this document must be evaluated against this reality, not against an idealized framework.

### Document Diagnosis

This document catalogs 43 techniques and 61 references. It is a literature survey with implementation pseudocode bolted on. Most "high priority" ratings reflect what would be intellectually interesting, not what DTT's architecture can absorb without a rewrite. Several techniques rated "High" (PDR/IC3, Pulse, CEGAR, CHC) would require replacing the analysis engine entirely.

Meanwhile, the practical gaps that cause real user-visible false positives and false negatives — else-branch negation, post-loop variable values, ternary tracking, range-for bounds, switch-case proofs — are not covered by any of the 43 cataloged techniques. They are AST pattern-matching work, ~50-300 lines each.

### Revised Priority Tiers

| Tier | What | Effort | Closes Real Gaps? |
|------|------|--------|-------------------|
| **Tier 1: Do Now** | Else-branch negation, post-loop values, value range narrowing from guards | ~50-200 lines each | Yes — eliminates classes of false positives |
| **Tier 2: Do Next** | Sparse analysis pre-pass (#38), context-sensitive summaries (#39), incremental Z3 (#29) | ~100-800 lines each | Yes — scalability and precision |
| **Tier 3: Do When Needed** | Summary serialization (#22), taint analysis (#19), recurrence solving (simplified #35) | 1-3 weeks each | Yes — new capabilities |
| **Tier 4: Interesting But Doesn't Fit** | PDR (#40), Pulse (#21), CHC (#23), CEGAR (#13), k-Induction (#34) | Multi-month rewrites | No — requires different architecture |
| **Tier 5: Academic Reference** | ISL (#25), ACDL (#20), Liquid Types (#14), Gradual Types (#6), BMC (#18) | N/A | No — informational only |

### What's Missing From This Document

The document does not discuss the features that would close DTT's *known, user-visible gaps*:

| Gap | What It Is | Effort | Academic Technique |
|-----|-----------|--------|-------------------|
| Else-branch negation proofs | `if (n < 10)` → else knows `n >= 10` | ~150 lines | None needed (AST work) |
| Post-loop variable values | `i == n` after `for(i=0;i<n;i++)` | ~50 lines | Simplified #35 |
| Ternary expression tracking | `size_t n = cond ? a : b;` loses value | ~100 lines | None needed |
| Range-for loop bounds | `for (auto& x : vec)` — implicit bound | ~150 lines | None needed |
| Switch-case proofs | `switch(v.index())` doesn't establish variant facts | ~200 lines | None needed |
| Assignment through references | `int& r = n; r = 5;` — alias not tracked | ~300 lines | Simplified #2 |

These are described in detail in the new **Part 0** below.

---

## Current Implementation Status

**Branch:** `claude/dtt-dual-mechanisms-Gd5bt`

### Already Implemented (This Branch)

| Feature | Status | Key Functions | Test File |
|---------|--------|---------------|-----------|
| **Transitive Precondition Propagation** | ✅ Done | `propagatePreconditionsTransitively()` | `950_transitive_precond_xfail.cpp` |
| **Frame Inference (Effect Tracking)** | ✅ Done | `propagateFrameEffectsTransitively()`, `paramWrites` | `952_frame_inference_xfail.cpp` |
| **Refutations (Counterexamples)** | ✅ Done | `buildCounterexample()`, `NoteCounterexample` | `951_refutation_xfail.cpp` |

**Code Locations:**
- `src/DTTPlugin.cpp:4543` - `propagatePreconditionsTransitively()` with fixpoint iteration
- `src/DTTPlugin.cpp:4662` - `propagateFrameEffectsTransitively()`
- `src/DTTPlugin.cpp:5251` - `buildCounterexample()` for refutation notes

### Not Yet Implemented (This Document)

| Feature | Status | Complexity |
|---------|--------|------------|
| **#3: Widening/Narrowing** | 📋 Planned | Medium-High |
| **#5: Incorrectness Logic** | 📋 Planned | Medium |

---

## Executive Summary

### Immediate Implementation (Parts 0-3)

> **[REVIEW]** Original plan estimated 10-14 weeks for widening + incorrectness logic. The revised
> recommendation is to do the practical gap closures (Part 0) first — they take ~1-2 weeks total
> and close more real bugs than the full widening/incorrectness effort. Phase 5A (else-branch
> negation) is the single highest-value item in this entire document.

| Feature | Complexity | Effort | Priority | Review |
|---------|------------|--------|----------|--------|
| **Part 0: Practical gap closures** | Low | 1-2 weeks | **Highest** | NEW — not in original |
| Phase 5A: Else-branch negation | Low | 2-3 days | **Highest** | Extracted from Part 2 |
| Phase 5B: Path condition accumulation | Medium | 1-2 weeks | High | From Part 2, standalone value |
| #3 Widening/Narrowing (full) | Medium-High | 4-6 weeks | Medium | Demoted — see review notes |
| Phase 5C-D: Feasibility + Join merging | Medium-High | 4-5 weeks | Lower | Only after 5A/5B proven |
| Shared Infrastructure | Medium | 2-3 weeks | Required | Unchanged |

### Literature Survey (Parts 7-9) — Revised Ratings

> **[REVIEW]** The original ratings had 9 techniques at "High" priority. Revised ratings reflect
> architectural fit: techniques that require a CFG, worklist, or heap model are demoted regardless
> of their academic merit. Techniques that work within the existing AST visitor are promoted.

| Status | Count | Techniques | Review Change |
|--------|-------|------------|---------------|
| ✅ Implemented | 5 | #1 Frame, #4 Effects, #10 Refutations, #42 TypeState (ghost fields), #43 Value Range (partial) | +2: #42 and #43 already exist |
| 📋 In Plan | 2 | #3 Widening, #5 Incorrectness | Unchanged |
| 🔴 **Fits DTT Now** | 3 | #38 Sparse, #39 k-CFA, #29 Inc. SMT | Reduced from 9: only techniques compatible with current architecture |
| 🟡 **Valuable, Moderate Effort** | 4 | #22 Incremental, #19 Taint, #35 Recurrence (simplified), #7 Ownership | #22 and #19 have real user demand |
| 🟠 **Doesn't Fit Architecture** | 6 | #40 PDR, #21 Pulse, #23 CHC, #13 CEGAR, #11 Demand-Driven, #34 k-Induction | Demoted — require CFG or engine rewrite |
| ⚪ Medium | 8 | #8 Relational, #12 Predicate, #16 Octagons, #17 Merging, #24 SeaHorn, #25 ISL, #28 Lifetime, #37 Templates | Unchanged |
| ⚫ Low/Info | 11 | #2, #6, #9, #14, #18, #20, #26, #27, #30-33, #36, #41 | +2: #36 Acceleration, #41 Datalog demoted |

**Total Techniques Cataloged**: 54 (43 original + 11 from 2022-2026 survey, with 77 references)

---

## Part 0: Practical Gaps Not Covered by Literature (NEW)

> **[REVIEW]** These are the gaps that cause real user-visible false positives and false negatives.
> None of them map to any of the 43 cataloged academic techniques. They are AST pattern-matching
> work — small, testable, high-value changes to the existing visitor.

### 0.1 Else-Branch Negation Proofs

**Priority: Highest. Effort: ~150 lines. Closes: false positives from missing negation.**

DTT's `TraverseIfStmt` already saves/restores proof state for then/else branches. But it only adds
positive proofs in the then-branch — it does not add the negated proof in the else-branch or
after an early-return guard.

```cpp
// TODAY: DTT knows n >= 10 inside the if-body, but after early return
// it does NOT know n < 10 (the negation).
void f(int* arr, size_t n) {
    if (n >= 10) {
        // DTT knows: n >= 10
    }
    // After: DTT knows NOTHING about n ← false positive source
}

// ALSO TODAY: else-branch gets no proofs
void g(int* arr, size_t n) {
    if (n == 0) {
        return;
    }
    // DTT should know: n != 0, so n >= 1
    arr[0] = 42;  // False positive: DTT can't prove n >= 1
}
```

**Fix:** In `TraverseIfStmt`, after processing the then-branch, add the negated guard to the
else-branch (or to the continuation when the then-branch terminates). The negation mapping is
trivial: `<` ↔ `>=`, `<=` ↔ `>`, `==` ↔ `!=`.

This is Phase 5A from Part 2, extracted because it stands alone and has the highest value-to-effort
ratio of anything in this document.

### 0.2 Post-Loop Variable Values

**Priority: High. Effort: ~50 lines. Closes: false negatives after loops.**

After `for (size_t i = 0; i < n; i++)`, DTT loses `i`'s value. But `i == n` at the exit point.
This is needed for patterns like:

```cpp
size_t count = 0;
for (auto& x : items) {
    if (pred(x)) count++;
}
// count <= items.size() — but DTT doesn't know this
```

**Fix:** At the end of `TraverseForStmt`, record `loopVar == bound` in `TypedEnv` when the loop
exits normally (no break). For `<` bounds, `i == n`; for `<=` bounds, `i == n + 1`.

### 0.3 Value Range Narrowing From Guards

**Priority: High. Effort: ~200 lines. Closes: false positives from unconstrained symbolic values.**

DTT doesn't narrow symbolic values through guard chains:

```cpp
void f(char* buf, size_t n) {
    if (n > 100) return;
    // n is in [0, 100] but DTT still treats n as unconstrained
    memcpy(buf, "data", n);  // False positive
}
```

**Fix:** When `TraverseIfStmt` processes an early-return then-branch, add the negated guard to
the proof store for the continuation. This overlaps with 0.1 but specifically targets the
`if (bad) return;` pattern that is extremely common in defensive C/C++.

### 0.4 Ternary Expression Tracking

**Priority: Medium. Effort: ~100 lines. Closes: lost values through conditional expressions.**

```cpp
size_t bound = (n > 100) ? 100 : n;
// DTT loses track: bound is UNKNOWN
for (size_t i = 0; i < bound; i++) {
    buf[i] = 'x';  // Can't verify — bound is unknown
}
```

**Fix:** Add a `VisitConditionalOperator` handler that evaluates both arms. When both arms are
known constants, record the range. When one arm is a parameter, record the constraint relationship.

### 0.5 Range-For Loop Bounds

**Priority: Medium. Effort: ~150 lines. Closes: false positives in modern C++ iteration.**

```cpp
for (auto& x : vec) {
    // Implicit: iterates vec.size() times
    // DTT doesn't extract this bound
}
```

**Fix:** In `TraverseForStmt` (or `TraverseCXXForRangeStmt`), detect the range expression and
extract the container's `_size` property as the implicit loop bound.

### 0.6 Switch-Case Variant Proofs

**Priority: Medium. Effort: ~200 lines. Closes: false positives in variant access patterns.**

```cpp
std::variant<int, string> v = ...;
switch (v.index()) {
    case 0: std::get<0>(v); break;  // DTT warns — doesn't know index == 0 here
    case 1: std::get<1>(v); break;  // DTT warns — doesn't know index == 1 here
}
```

**Fix:** Add `TraverseSwitchStmt` that, for each case label, adds a proof
`switchVar._variant_index == caseValue` to the proof store within that case body.

### 0.7 Assignment Through References (Local Aliases)

**Priority: Lower. Effort: ~300 lines. Closes: lost values through reference bindings.**

```cpp
int& r = n;
r = 5;
// DTT doesn't know n == 5 now
```

**Fix:** Maintain a local alias map (`DenseMap<VarDecl*, VarDecl*>`) for reference bindings.
When a reference target is assigned, propagate to the aliased variable. This is a simplified
version of #2 (Must/May Alias) — only for local, single-level references with no ambiguity.

### Part 0 Implementation Order

| # | Gap | Lines | Depends On | Test File Pattern |
|---|-----|-------|------------|-------------------|
| 0.1 | Else-branch negation | ~150 | None | `970_path_else_negation_pass.cpp` |
| 0.2 | Post-loop values | ~50 | None | `960_postloop_value_pass.cpp` |
| 0.3 | Guard range narrowing | ~200 | 0.1 (overlaps) | `971_guard_narrowing_pass.cpp` |
| 0.4 | Ternary tracking | ~100 | None | `963_ternary_tracking_pass.cpp` |
| 0.5 | Range-for bounds | ~150 | None | `964_rangefor_bounds_pass.cpp` |
| 0.6 | Switch-case proofs | ~200 | None | `975_switch_variant_pass.cpp` |
| 0.7 | Reference aliases | ~300 | None | `976_ref_alias_pass.cpp` |

**Total: ~1150 lines, 1-2 weeks. Closes more real gaps than the rest of this document combined.**

---

## Part 1: Widening/Narrowing for Loops

> **[REVIEW]** This part is well-structured but over-scoped. Phase 3A (minimal widening) is useful
> for while-loops with computed bounds. Phase 3B (fixpoint iteration) is where the cost/benefit
> ratio drops — it requires re-traversing the loop body multiple times, which the single-pass AST
> visitor doesn't naturally support. Phase 3C (CFG migration) is a different project entirely.
>
> **Recommendation:** Implement Phase 3A only. For the common `for(i=0;i<n;i++)` case, DTT already
> extracts the bound. What's missing is post-loop values (Part 0.2) and computed bounds (Part 0.4),
> which are simpler solutions to the same user-visible problems. Full widening/narrowing is only
> needed for while-loops with loop-carried dependencies — a real but less common pattern.

### 1.1 Problem Statement

DTT cannot verify buffer safety in loops where the index bound is not directly extractable from the loop condition.

**Current Limitation:**
```cpp
void fill_buffer(char* buf, size_t capacity) {
    size_t len = 0;
    while (len < capacity) {
        buf[len] = 'x';  // DTT: "cannot verify buf._size_bytes > len"
        len++;           // DTT loses track of len's relationship to capacity
    }
}
```

**Goal:**
```cpp
void fill_buffer(char* buf, size_t capacity) {
    size_t len = 0;
    while (len < capacity) {
        buf[len] = 'x';  // DTT: PASS with precondition buf._size_bytes >= capacity
        len++;
    }
}
```

### 1.2 Current State

**Existing Loop Infrastructure** (`src/DTTPlugin.cpp`):

| Component | Lines | Status |
|-----------|-------|--------|
| `TraverseForStmt` | 8117-8467 | Extracts bounds from `for(i=0; i<N; i++)` |
| `TraverseWhileStmt` | 8011-8087 | Extracts guards like `i < v.size()` |
| `VarBoundProof` | 2557-2610 | Links loop var to bound |
| `extractVarGuard` | 7137+ | Parses condition expressions |
| Z3 integration | Throughout | Symbolic constraint solving |

**What Works:**
- Simple `for (int i=0; i<10; i++)` with constant bound
- Field-based bounds: `for (int i=0; i<v.size(); i++)`
- Symbolic bounds: `for (int i=0; i<n; i++)`
- Step values > 1: `for (int i=0; i<20; i+=2)`

**What Doesn't Work:**
- Loop-carried dependencies
- Computed bounds: `size_t bound = min(n, 100);`
- Post-loop variable values
- Nested loop interactions
- While loops with complex exit conditions

### 1.3 Technical Approach

#### Abstract Interpretation Background

**Widening (▽)**: Accelerate convergence by jumping to a safe over-approximation
```
State₀ = {len: 0}
State₁ = {len: 0 or 1}
State₂ = {len: 0 or 1 or 2}
...
Widened = {len: [0, ∞)}  // Jump to interval
```

**Narrowing (△)**: Refine over-approximation using constraints
```
Widened = {len: [0, ∞)}
Guard = len < capacity
Narrowed = {len: [0, capacity-1]}  // Inside loop body
```

#### Implementation Strategy

**Phase 3A: Minimal Widening (2 weeks)**

Add widening to existing AST traversal without CFG migration.

```cpp
// New structure for widened state
struct WidenedValue {
    enum Kind { EXACT, INTERVAL, UNKNOWN };
    Kind kind;
    std::optional<int64_t> lower;  // None = -∞
    std::optional<int64_t> upper;  // None = +∞
    std::string symbolicUpper;     // e.g., "capacity"
};

// New map for loop variable states
llvm::DenseMap<std::string, WidenedValue> LoopVarStates;
```

**Key Functions to Add:**

```cpp
/// Widen a variable's value after seeing multiple iterations
WidenedValue widenValue(const WidenedValue& prev, const SymValue& current);

/// Narrow a widened value using a guard condition
WidenedValue narrowWithGuard(const WidenedValue& wide, const Expr* guard);

/// Check if widened value satisfies a bound
bool checkWidenedBound(const WidenedValue& idx, const SymValue& bound);
```

**Phase 3B: Fixpoint Iteration (2 weeks)**

Add proper fixpoint detection for loop analysis.

```cpp
/// Analyze a loop body until fixpoint
void analyzeLoopToFixpoint(Stmt* body, Expr* cond, unsigned maxIter = 10) {
    auto prevState = captureState();

    for (unsigned i = 0; i < maxIter; ++i) {
        // Narrow state with loop condition
        auto narrowed = narrowWithGuard(prevState, cond);

        // Traverse body with narrowed state
        TraverseStmt(body);

        // Widen for next iteration
        auto newState = widenState(prevState, captureState());

        if (statesEqual(prevState, newState)) {
            break;  // Fixpoint reached
        }
        prevState = newState;
    }
}
```

**Phase 3C: CFG Migration (Optional, 2-4 weeks)**

If Phase 3A/3B prove insufficient, migrate to Clang's CFG.

```cpp
#include "clang/Analysis/CFG.h"

/// Build CFG and analyze with dataflow
void analyzeWithCFG(FunctionDecl* FD) {
    auto cfg = CFG::buildCFG(FD, FD->getBody(), &Ctx, CFG::BuildOptions());

    // Forward dataflow analysis
    llvm::DenseMap<const CFGBlock*, AnalysisState> blockStates;

    // Worklist algorithm with widening at loop headers
    std::queue<const CFGBlock*> worklist;
    // ... implementation
}
```

### 1.4 Files to Modify

| File | Changes |
|------|---------|
| `src/DTTPlugin.cpp` | Add `WidenedValue`, fixpoint iteration, narrowing logic |
| `src/DTTTypes.h` | Add `WidenedValue` struct definition |
| `examples/960_*.cpp` | New test cases for widening/narrowing |

### 1.5 Test Cases

```cpp
// examples/960_widening_basic_pass.cpp
void test_widening_basic(char* buf, size_t cap) {
    for (size_t i = 0; i < cap; i++) {
        buf[i] = 'x';  // Should pass with precondition buf._size_bytes >= cap
    }
}

// examples/961_widening_nested_pass.cpp
void test_nested_loops(int* matrix, size_t rows, size_t cols) {
    for (size_t i = 0; i < rows; i++) {
        for (size_t j = 0; j < cols; j++) {
            matrix[i * cols + j] = 0;  // Complex index expression
        }
    }
}

// examples/962_narrowing_early_exit_pass.cpp
void test_narrowing_exit(char* buf, size_t cap) {
    size_t i = 0;
    while (true) {
        if (i >= cap) break;
        buf[i] = 'x';  // Narrowing: i < cap here
        i++;
    }
}

// examples/963_widening_computed_bound_xfail.cpp
void test_computed_bound(char* buf, size_t n) {
    size_t bound = (n > 100) ? 100 : n;
    for (size_t i = 0; i < bound; i++) {
        buf[i] = 'x';  // Requires tracking ternary result
    }
}
```

---

## Part 2: Incorrectness Logic

> **[REVIEW]** The framing here conflates two different things:
>
> 1. **Phase 5A (negation tracking)** and **5B (path conditions)** — these are concrete, high-value
>    improvements to DTT's existing `TraverseIfStmt`. They don't require "incorrectness logic" as a
>    theoretical framework. They're just "track what we know in the else branch." Phase 5A is the
>    single most valuable item in this document and is now listed as Part 0.1.
>
> 2. **Phase 5C (feasibility checking)** and **5D (join-point merging)** — these are genuinely more
>    complex and *do* benefit from the incorrectness logic framing. But they're also where the
>    cost/benefit ratio drops. Feasibility checking calls Z3 on every branch (performance concern).
>    Join-point merging requires a lattice that DTT's proof store doesn't currently support.
>
> **Recommendation:** Do 5A immediately (it's in Part 0.1). Do 5B next (standalone value for
> diagnostics). Defer 5C and 5D until 5A/5B are proven in practice.

### 2.1 Problem Statement

DTT reports "cannot verify" for code that is **definitely safe** on all reachable paths, because it conservatively considers all paths.

**Current False Positive:**
```cpp
void maybe_overflow(char* buf, size_t n, bool flag) {
    if (flag) {
        memcpy(buf, "data", n);  // Dangerous path
    }
}

void caller() {
    char small[4];
    maybe_overflow(small, 100, false);  // DTT warns, but flag=false!
}
```

**Goal:** Only warn when the bug path is **actually reachable**.

### 2.2 Incorrectness Logic Background

Traditional Hoare Logic: `{P} C {Q}` means "if P holds before C, then Q holds after"
- Proves absence of bugs (may have false positives)

Incorrectness Logic (O'Hearn 2019): `[P] C [Q]` means "if Q holds after C, there exists a path from P"
- Proves presence of bugs (may have false negatives)
- Dual of traditional verification

**Key Insight for DTT:** Track conditions under which bugs are reachable, not just whether bugs might exist.

### 2.3 Current State

**Existing Path-Sensitive Features:**

| Component | Lines | Status |
|-----------|-------|--------|
| `PointerLifetimes` save/restore | 7692 | Tracks state per branch |
| `extractVarGuard` | 7137+ | Parses `i < v.size()` guards |
| `extractAllGuards` | 7207+ | Handles compound AND conditions |
| `doesBlockTerminate` | ~7700 | Detects early returns |
| ProofContext entailment | 627-767 | Implication reasoning |

**What Works:**
- Early return narrowing: `if (!p) return; // p is nonnull after`
- Loop guard proofs: `while (i < n) { // i < n holds in body }`
- Compound conditions: `if (x > 0 && y > 0) { ... }`

**What Doesn't Work:**
- Else-branch negation tracking
- Join-point state merging (LUB)
- Feasibility detection (pruning dead branches)
- Complex control flow (switch, goto)

### 2.4 Technical Approach

#### Phase 5A: Negation Guard Tracking (2 weeks)

Track what we know when a condition is **false**.

```cpp
// Extend GuardInfo with negation
struct GuardInfo {
    std::string basePath, field, proofKey;
    std::string negProofKey;      // Existing
    ProofOp positiveOp;           // New: op when guard is true
    ProofOp negativeOp;           // New: op when guard is false
};

// Add negation proofs in else-branches
bool TraverseIfStmt(IfStmt* If) {
    auto guard = extractVarGuard(If->getCond());

    // Then-branch: add positive proof
    addVarBoundProof(guard, guard.op);
    TraverseStmt(If->getThen());
    removeVarBoundProof(guard);

    // Else-branch: add NEGATED proof  <-- NEW
    if (If->getElse()) {
        addVarBoundProof(guard, guard.negOp);
        TraverseStmt(If->getElse());
        removeVarBoundProof(guard);
    }
}
```

**Negation Mapping:**

| Original Op | Negated Op |
|-------------|------------|
| `<` | `>=` |
| `<=` | `>` |
| `>` | `<=` |
| `>=` | `<` |
| `==` | `!=` |
| `!=` | `==` |

#### Phase 5B: Path Condition Accumulation (2 weeks)

Track the conditions required to reach each program point.

```cpp
// New: accumulated path condition
struct PathCondition {
    std::vector<std::pair<std::string, ProofOp>> conjuncts;

    void addConjunct(StringRef var, ProofOp op);
    void removeConjunct(StringRef var);
    bool implies(StringRef var, ProofOp op) const;
    std::string toString() const;
};

// Thread-local path condition
PathCondition CurrentPathCondition;

// Update warnings to include path condition
void emitConditionalWarning(SourceLocation loc, StringRef msg) {
    if (CurrentPathCondition.isEmpty()) {
        Diag.Report(loc, DiagID) << msg;
    } else {
        Diag.Report(loc, DiagID) << msg;
        Diag.Report(loc, NotePathCondition)
            << "reachable when: " << CurrentPathCondition.toString();
    }
}
```

#### Phase 5C: Feasibility Checking (2 weeks)

Use Z3 to detect infeasible paths.

```cpp
/// Check if current path condition is satisfiable
bool isPathFeasible() {
    z3::solver s(Z3Ctx);

    for (const auto& [var, op] : CurrentPathCondition.conjuncts) {
        s.add(encodeConstraint(var, op));
    }

    return s.check() == z3::sat;
}

// In TraverseIfStmt:
bool TraverseIfStmt(IfStmt* If) {
    auto guard = extractVarGuard(If->getCond());

    // Then-branch
    CurrentPathCondition.addConjunct(guard.var, guard.op);
    if (isPathFeasible()) {  // <-- Skip infeasible paths
        TraverseStmt(If->getThen());
    }
    CurrentPathCondition.removeConjunct(guard.var);

    // Else-branch
    CurrentPathCondition.addConjunct(guard.var, guard.negOp);
    if (isPathFeasible()) {
        TraverseStmt(If->getElse());
    }
    CurrentPathCondition.removeConjunct(guard.var);
}
```

#### Phase 5D: Join-Point Merging (2-3 weeks)

Implement proper state merging at control flow join points.

```cpp
// Lattice for proof states
enum class ProofState {
    BOTTOM,     // No information (unreachable)
    PROVEN,     // Definitely holds
    DISPROVEN,  // Definitely doesn't hold
    UNKNOWN,    // May or may not hold
    TOP         // Contradiction (error)
};

// Lattice operations
ProofState join(ProofState a, ProofState b) {
    if (a == ProofState::BOTTOM) return b;
    if (b == ProofState::BOTTOM) return a;
    if (a == b) return a;
    return ProofState::UNKNOWN;
}

// State at a program point
struct AnalysisState {
    llvm::DenseMap<std::string, ProofState> proofs;
    PathCondition pathCond;

    AnalysisState join(const AnalysisState& other) const;
    bool operator==(const AnalysisState& other) const;
};

// Merge states after if-else
bool TraverseIfStmt(IfStmt* If) {
    auto preState = captureState();

    // Analyze then-branch
    enterBranch(guard, /*negated=*/false);
    TraverseStmt(If->getThen());
    auto thenState = captureState();

    // Analyze else-branch
    restoreState(preState);
    enterBranch(guard, /*negated=*/true);
    TraverseStmt(If->getElse());
    auto elseState = captureState();

    // Merge at join point
    auto joinedState = thenState.join(elseState);
    restoreState(joinedState);
}
```

### 2.5 Files to Modify

| File | Changes |
|------|---------|
| `src/DTTPlugin.cpp` | Path condition tracking, negation guards, feasibility checks |
| `src/DTTTypes.h` | `PathCondition`, `ProofState`, `AnalysisState` structs |
| `examples/970_*.cpp` | New test cases for path sensitivity |

### 2.6 Test Cases

```cpp
// examples/970_path_else_negation_pass.cpp
void test_else_negation(int* arr, size_t n) {
    if (n == 0) {
        return;
    }
    // DTT should know: n != 0, so n >= 1
    arr[0] = 42;  // Safe: n >= 1 implies arr has at least 1 element
}

// examples/971_path_infeasible_pass.cpp
void test_infeasible_path(char* buf) {
    int x = 5;
    if (x > 10) {
        // This path is infeasible (x == 5, not > 10)
        buf[1000000] = 'x';  // Should not warn - unreachable
    }
}

// examples/972_path_conditional_bug_fail.cpp
void test_conditional_bug(char* buf, size_t n, bool flag) {
    if (flag) {
        memcpy(buf, "data", n);  // Bug when flag=true AND n > buf._size_bytes
    }
}

void caller_safe() {
    char small[4];
    test_conditional_bug(small, 100, false);  // PASS: flag=false
}

void caller_unsafe() {
    char small[4];
    test_conditional_bug(small, 100, true);   // FAIL: flag=true, 100 > 4
}

// examples/973_path_join_unknown_pass.cpp
void test_join_point(int* arr, size_t n, bool cond) {
    size_t idx;
    if (cond) {
        idx = 0;
    } else {
        idx = 1;
    }
    // After join: idx is either 0 or 1, so idx < 2
    if (n >= 2) {
        arr[idx] = 42;  // Safe: idx < 2 <= n
    }
}

// examples/974_path_switch_xfail.cpp
void test_switch_paths(char* buf, int cmd) {
    switch (cmd) {
        case 0: buf[0] = 'a'; break;
        case 1: buf[1] = 'b'; break;
        default: break;
    }
    // Requires switch path tracking (Phase 5D+)
}
```

---

## Part 3: Shared Infrastructure

### 3.1 State Capture/Restore

Both features need robust state management.

```cpp
/// Complete analysis state snapshot
struct AnalysisSnapshot {
    // From existing code
    llvm::DenseMap<std::string, SymValue> VarEnv;
    llvm::DenseMap<std::string, Lifetime> PointerLifetimes;
    std::vector<std::pair<std::string, std::string>> Proofs;

    // New for widening
    llvm::DenseMap<std::string, WidenedValue> LoopVarStates;

    // New for path sensitivity
    PathCondition pathCond;
};

AnalysisSnapshot captureState();
void restoreState(const AnalysisSnapshot& snap);
```

### 3.2 Enhanced ProofContext

Extend entailment for interval reasoning.

```cpp
class ProofContext {
public:
    // Existing
    bool canProve(StringRef lhs, ProofOp op, const SymValue& rhs);

    // New: interval-aware proving
    bool canProveInterval(StringRef var, const WidenedValue& interval,
                          ProofOp op, const SymValue& bound);

    // New: path-condition-aware proving
    bool canProveUnderCondition(const PathCondition& cond,
                                StringRef var, ProofOp op, const SymValue& bound);
};
```

### 3.3 Diagnostic Enhancements

Better error messages with path/loop context.

```cpp
// New diagnostic IDs
unsigned NoteLoopIteration;    // "in loop iteration with %0 in range [%1, %2]"
unsigned NotePathCondition;    // "reachable when: %0"
unsigned NoteInfeasiblePath;   // "path is infeasible: %0 contradicts %1"
unsigned NoteJoinedState;      // "after branch merge: %0 is unknown"
```

---

## Part 4: Implementation Schedule

> **[REVIEW]** Original 14-week schedule is superseded. Revised schedule below prioritizes the
> practical gaps (Part 0) that close real bugs, then the Phase 5A/5B work that has standalone value,
> then widening Phase 3A if needed. See Revised Action Plan (Part 10) at end of document.

### Week 1-2: Foundation
- [ ] Add `WidenedValue` struct to `DTTTypes.h`
- [ ] Add `PathCondition` struct to `DTTTypes.h`
- [ ] Implement state capture/restore functions
- [ ] Write basic test cases (960-963, 970-973)

### Week 3-4: Widening Phase A
- [ ] Implement `widenValue()` function
- [ ] Implement `narrowWithGuard()` function
- [ ] Modify `TraverseForStmt` to use widening
- [ ] Modify `TraverseWhileStmt` to use widening
- [ ] Run tests, fix regressions

### Week 5-6: Widening Phase B
- [ ] Implement fixpoint iteration for loops
- [ ] Add convergence detection
- [ ] Handle nested loops
- [ ] Performance testing with Z3

### Week 7-8: Incorrectness Phase A
- [ ] Implement negation guard extraction
- [ ] Add negation proofs in else-branches
- [ ] Extend `ProofOp` with negation mapping
- [ ] Update `extractVarGuard` for negation

### Week 9-10: Incorrectness Phase B
- [ ] Implement `PathCondition` accumulation
- [ ] Add path condition to diagnostics
- [ ] Implement Z3 feasibility checking
- [ ] Cache feasibility results

### Week 11-12: Incorrectness Phase C
- [ ] Implement `AnalysisState` with `ProofState` lattice
- [ ] Implement `join()` operation
- [ ] Modify `TraverseIfStmt` for proper merging
- [ ] Handle early returns correctly

### Week 13-14: Integration & Testing
- [ ] Run full test suite, fix regressions
- [ ] Performance optimization
- [ ] Documentation updates
- [ ] Convert xfail tests to fail/pass as appropriate

---

## Part 5: Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Widening loses precision | High | Medium | Conservative widening thresholds |
| Z3 timeout on complex loops | Medium | Medium | Caching, timeout limits |
| State explosion in path tracking | Medium | High | Limit path depth, merge aggressively |
| Regression in existing tests | Medium | High | Run full suite after each phase |
| CFG migration becomes necessary | Low | High | Design for CFG compatibility |
| String-based state fragility | Medium | Medium | Consider typed paths (future) |

---

## Part 6: Success Criteria

### Widening/Narrowing
- [ ] `960_widening_basic_pass.cpp` passes
- [ ] `961_widening_nested_pass.cpp` passes
- [ ] No regression in existing loop tests (707, 711, 712 series)
- [ ] Performance: < 2x slowdown on large files

### Incorrectness Logic
- [ ] `970_path_else_negation_pass.cpp` passes
- [ ] `971_path_infeasible_pass.cpp` passes (no warning on dead code)
- [ ] `972_path_conditional_bug_fail.cpp` correctly distinguishes safe/unsafe callers
- [ ] False positive rate reduced by measurable amount

### Overall
- [ ] All existing tests pass
- [ ] New xfail tests document remaining gaps
- [ ] Code follows LLVM style guidelines
- [ ] Implementation documented in code comments

---

## Part 7: Literature Survey - Additional Techniques

This section catalogs additional techniques from the literature that could enhance DTT's analysis capabilities.

> **[REVIEW]** The catalog below is a useful reference, but the priority ratings need recalibration
> against DTT's actual architecture. Each technique now has a **[REVIEW]** note on architectural
> fit. The key distinction: techniques that work *within* a single-pass AST visitor (add a map,
> add a check) vs. techniques that *replace* it (require CFG, worklist, or backward analysis).

### Already Implemented

| # | Technique | Status | Implementation |
|---|-----------|--------|----------------|
| 1 | Bi-Abduction (Frame Inference) | ✅ Done | `propagateFrameEffectsTransitively()` |
| 4 | Effect Systems | ✅ Done | `paramWrites` tracking |
| 10 | Refutations | ✅ Done | `buildCounterexample()` |

### In This Plan

| # | Technique | Section |
|---|-----------|---------|
| 3 | Widening/Narrowing | Part 1 |
| 5 | Incorrectness Logic | Part 2 |

### Remaining From Original 10

#### #2: Must/May Alias Dual

**Source:** Andersen 1994, Steensgaard 1996

Track both definite aliases (must) and possible aliases (may) to improve precision.

```cpp
void example(int* p, int* q, bool cond) {
    if (cond) p = q;        // May alias: p, q
    *p = 10;
    // Must alias: none (p could be original or q)
    // May alias: {p, q}
}
```

| Aspect | Value |
|--------|-------|
| **Effort** | High (needs points-to analysis) |
| **Value** | Medium |
| **Priority** | Low |

#### #6: Gradual Refinement Types

**Source:** Siek & Taha 2006, Lehmann & Tanter 2017

Mix static refinement checking with dynamic checks at boundaries.

```cpp
// Statically checked region
void inner(char* buf /* : {p | p._size >= 10} */, size_t n) {
    memcpy(buf, "data", n);  // Static check
}

// Dynamic boundary
void outer(char* buf, size_t n) {
    if (n <= buf._size) {    // Runtime check
        inner(buf, n);       // Crosses to static region
    }
}
```

| Aspect | Value |
|--------|-------|
| **Effort** | High |
| **Value** | Low (C++ is static) |
| **Priority** | Low |

#### #7: Ownership Transfer via Separation Logic

**Source:** Reynolds 2002, Rust ownership model

Track exclusive ownership and transfer on move.

```cpp
void transfer(std::unique_ptr<Buffer> p) {
    // p owns the buffer exclusively
    auto q = std::move(p);  // Ownership transferred to q
    // p is now empty (separation: p * q, but p = emp)
    use(q);                 // OK: q owns it
    use(p);                 // ERROR: p doesn't own anything
}
```

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | High (enhances move tracking) |
| **Priority** | Medium |

#### #8: Relational Properties

**Source:** Cousot & Halbwachs 1978

Track relationships between multiple variables.

```cpp
void copy_buffers(char* dst, char* src, size_t dst_cap, size_t src_len) {
    // Relational: src_len <= dst_cap
    if (src_len <= dst_cap) {
        memcpy(dst, src, src_len);  // Safe: relates dst_cap to src_len
    }
}
```

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | Medium (cross-param bounds) |
| **Priority** | Medium |

#### #9: Second-Order Bi-Abduction

**Source:** Calcagno et al. 2011

Infer preconditions for callbacks and higher-order functions.

```cpp
void for_each(int* arr, size_t n, void (*f)(int*)) {
    for (size_t i = 0; i < n; i++) {
        f(&arr[i]);  // What does f require about its argument?
    }
}
// Infer: f requires arg != null, arg points to valid int
```

| Aspect | Value |
|--------|-------|
| **Effort** | High |
| **Value** | Low (callbacks rare in DTT scope) |
| **Priority** | Low |

---

### New Techniques (11-20)

#### #11: Demand-Driven Analysis

**Source:** Horwitz & Reps 1995

Analyze backward from error sites instead of forward from entry.

```cpp
void f(char* buf, size_t n) {
    // ... 1000 lines ...
    memcpy(buf, src, n);  // START HERE: What do we need to prove?
    // Work backward to find: buf._size >= n required
}
```

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | **High** (performance) |
| **Priority** | 🔴 High |

**Why High Priority:** Only analyzes paths leading to potential errors. Faster on large codebases.

> **[REVIEW: DOESN'T FIT]** Demoted to 🟠. DTT is a forward-driven `RecursiveASTVisitor`. Demand-driven
> analysis requires backward traversal from error sites, which means either a CFG or a second
> pass over the AST. The visitor architecture visits nodes in source order — you can't "start at
> memcpy and work backward." The performance benefit is real, but the implementation cost is
> effectively "build a different analysis engine." #38 Sparse Analysis achieves a similar performance
> win within the existing architecture by skipping irrelevant variables during the forward pass.

#### #12: Predicate Abstraction

**Source:** SLAM (Ball & Rajamani 2001), BLAST (Henzinger et al. 2002)

Track boolean predicates instead of concrete values.

```cpp
// Instead of: buf._size = 100, n = symbolic
// Track predicates: {P1: n <= buf._size, P2: n >= 0}

if (n <= 100) {        // P1 becomes true
    memcpy(buf, src, n);  // Check P1, not concrete values
}
```

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | Medium |
| **Priority** | Medium |

#### #13: Interpolation-Based Refinement (CEGAR)

**Source:** McMillan 2003

Learn new predicates from infeasible paths.

```cpp
void f(char* buf, size_t n) {
    if (n > 100) return;
    // Spurious counterexample: n = 200, buf._size = 50
    // Interpolant learned: n <= 100
    memcpy(buf, src, n);  // Now provable with n <= 100
}
```

| Aspect | Value |
|--------|-------|
| **Effort** | High |
| **Value** | **High** (loop invariants) |
| **Priority** | 🟡 Medium-High |

**Why Medium-High:** Automatically discovers invariants that manual analysis misses.

> **[REVIEW: DOESN'T FIT]** Demoted to 🟠. CEGAR requires an iterative refinement loop:
> find spurious counterexample → compute interpolant → refine abstraction → re-analyze. DTT's
> single-pass visitor analyzes each function once. You'd need a fundamentally different structure
> where the entire function is re-analyzed with refined predicates. The value is real (loop invariant
> discovery) but the cost is a rewrite of the analysis loop, not an enhancement to the visitor.

#### #14: Liquid Types

**Source:** Rondon, Kawaguchi, Jhala 2008

SMT-backed refinement type inference.

```cpp
// Inferred types:
// buf : {p: char* | p._size_bytes >= n}
// n   : {v: size_t | v >= 0}
void copy(char* buf, size_t n) {
    memcpy(buf, "x", n);
}
```

| Aspect | Value |
|--------|-------|
| **Effort** | High |
| **Value** | Medium (types as docs) |
| **Priority** | Low |

#### #15: Summary-Based Interprocedural Analysis

**Source:** Reps, Horwitz, Sagiv 1995

Compute reusable function summaries.

```cpp
// Summary: inner(buf, n) requires buf._size >= n
void inner(char* buf, size_t n) { memcpy(buf, "x", n); }

// At call site, instantiate summary:
void outer(char* p, size_t m) {
    inner(p, m);  // Apply summary: check p._size >= m
}
```

| Aspect | Value |
|--------|-------|
| **Effort** | Low |
| **Value** | **High** |
| **Priority** | 🟢 Already partial (FnPreconditions) |

#### #16: Numeric Abstract Domains (Octagons/Polyhedra)

**Source:** Miné 2006

Track relational numeric constraints like `x - y <= 5`.

```cpp
void copy_offset(char* buf, size_t cap, size_t off, size_t len) {
    // Octagon: off + len <= cap
    if (off + len <= cap) {
        memcpy(buf + off, src, len);  // Safe via octagon
    }
}
```

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | Medium |
| **Priority** | Medium |

#### #17: Symbolic Execution with State Merging

**Source:** Kuznetsov et al. 2012

Merge states at join points to avoid path explosion.

```cpp
size_t len;
if (cond) { len = 10; } else { len = 20; }
// Merged: len ∈ [10, 20]
buf[len - 1] = 'x';  // Check: buf._size >= 20 (conservative)
```

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | Medium (scalability) |
| **Priority** | Medium |

#### #18: Bounded Model Checking

**Source:** CBMC (Clarke et al. 2004)

Unroll loops to bound K, encode as SAT.

```cpp
// Unroll 10 iterations:
for (int i = 0; i < n && i < 10; i++) {
    buf[i] = 'x';
}
// Encode: (i=0 → safe) ∧ (i=1 → safe) ∧ ... ∧ (i=9 → safe)
```

| Aspect | Value |
|--------|-------|
| **Effort** | Low |
| **Value** | Low (testing, not verification) |
| **Priority** | Low |

#### #19: Taint Analysis

**Source:** Livshits & Lam 2005

**📋 See:** [`docs/plans/TAINT_TRACKING_PROPOSAL.md`](TAINT_TRACKING_PROPOSAL.md) for detailed design.

Track untrusted input flow to sensitive operations.

```cpp
size_t n = read_from_network();  // TAINTED
if (n > MAX_SIZE) return;        // Sanitized
memcpy(buf, data, n);            // Safe: taint cleared
```

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | **High** (Heartbleed-class bugs) |
| **Priority** | 🔴 High |
| **Existing Plan** | `TAINT_TRACKING_PROPOSAL.md` (29KB, detailed design) |

**Why High Priority:** Directly addresses bugs where attacker controls size parameter.

**Proposal covers:**
- Option A: DTT-native (copy CSA rules) vs Option B: Link CSA libraries
- Taint sources (`recv`, `read`, `fread`, etc.)
- Taint propagators and sanitizers
- Integration with existing DTT diagnostics

#### #20: Abstract Conflict-Driven Learning (ACDL)

**Source:** D'Silva et al. 2014

Apply CDCL SAT techniques to abstract interpretation.

```cpp
// Analysis fails: x > 100 ∧ x < 50 (contradiction)
// Learn conflict clause: ¬(x > 100 ∧ x < 50)
// Future paths with this combination pruned
```

| Aspect | Value |
|--------|-------|
| **Effort** | High |
| **Value** | Medium |
| **Priority** | Low (research) |

---

### Priority Summary (Original)

| Priority | Techniques |
|----------|------------|
| 🔴 **High** | #11 Demand-Driven, #19 Taint Analysis (has detailed plan) |
| 🟡 **Medium-High** | #13 Interpolation/CEGAR |
| 🟢 **Already Partial** | #15 Summaries |
| ⚪ **Medium** | #7 Ownership, #8 Relational, #12 Predicate, #16 Octagons, #17 Merging |
| ⚫ **Low** | #2 Alias, #6 Gradual, #9 2nd-Order, #14 Liquid, #18 BMC, #20 ACDL |

### Priority Summary (Revised)

> **[REVIEW]** #11 and #13 demoted because they don't fit the AST visitor architecture. #19 remains
> valuable but is a separate feature, not a core analysis improvement. #7 and #15 promoted because
> they enhance capabilities DTT already has.

| Priority | Techniques | Rationale |
|----------|------------|-----------|
| 🟡 **Valuable** | #19 Taint (separate feature), #7 Ownership (enhances move tracking) | Real user demand, feasible |
| 🟢 **Already Partial** | #15 Summaries (FnPreconditions exists) | Extend, don't build |
| ⚪ **Medium** | #8 Relational, #12 Predicate, #16 Octagons, #17 Merging | Useful but not urgent |
| 🟠 **Doesn't Fit** | #11 Demand-Driven, #13 CEGAR | Require backward analysis / re-analysis loop |
| ⚫ **Low** | #2 Alias, #6 Gradual, #9 2nd-Order, #14 Liquid, #18 BMC, #20 ACDL | Unchanged |

### Recommended Next Steps (Revised)

1. **#19 Taint Analysis** - Detailed plan exists, separate feature track
2. **#7 Ownership** - Enhances existing `std::move` tracking with low risk
3. **#15 Summaries** - Extend FnPreconditions with return value tracking
4. ~~#11 Demand-Driven~~ — replaced by #38 Sparse Analysis (Part 9)
5. ~~#13 CEGAR~~ — deferred until CFG infrastructure exists

---

## Part 8: SOTA Tools & Techniques (21-32)

Additional state-of-the-art techniques from industrial tools and recent research. Consolidated by category.

### Compositional Analysis

#### #21: Infer Pulse (Meta)

**Source:** Calcagno et al. 2015, Distefano et al. 2019

Meta's compositional memory safety analyzer. Scales to millions of LOC via bi-abductive summaries.

```cpp
// Pulse computes summary:
// inner: requires p != null, ensures p->data initialized
void inner(Obj* p) { p->data = 1; }

// At call site, instantiate and check:
void outer(Obj* q) {
    inner(q);  // Check: q != null
    use(q->data);  // Safe: summary guarantees initialized
}
```

**Key innovations:**
- Compositional bi-abduction (analyze once, reuse summary)
- Incremental re-analysis on code changes
- Separation logic for heap reasoning

| Aspect | Value |
|--------|-------|
| **Effort** | High (architectural change) |
| **Value** | **Very High** (scalability) |
| **Priority** | 🔴 High |
| **Reference** | Calcagno et al., "Moving Fast with Software Verification" (ICSE 2015) |

> **[REVIEW: DOESN'T FIT]** Demoted to 🟠. Pulse's value comes from separation logic heap reasoning
> and bi-abductive frame inference. DTT tracks named fields on typed variables, not heap cells.
> The `VarToResource` aliasing in DTT is ad-hoc, not a separation logic frame. Building Pulse-style
> analysis means replacing DTT's proof store with a separation logic engine — a multi-month
> architectural change. The *incremental summary reuse* aspect of Pulse is achievable without
> separation logic (see #22) and is the genuinely valuable piece to extract.

#### #22: Incremental & Modular Analysis

**Source:** Arzt 2014, Szabó 2021, Cousot 2019

Re-analyze only changed code and its dependents. Essential for CI integration.

```cpp
// Change: modify inner()
// Dependency graph: outer() → middle() → inner()

void inner(char* buf, size_t n) { /* CHANGED */ }
void middle(char* buf, size_t n) { inner(buf, n); }  // Re-analyze
void outer(char* buf, size_t n) { middle(buf, n); }  // Re-analyze
void unrelated() { ... }  // SKIP - no dependency
```

**Key innovations:**
- Dependency tracking between functions
- Invalidation propagation on changes
- Summary caching with version stamps

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | **High** (CI performance) |
| **Priority** | 🔴 High |
| **References** | Arzt et al., "Reviser" (ISSTA 2014); Szabó et al., "Incrementalizing Lattice-Based Analyses" (OOPSLA 2021) |

> **[REVIEW: FITS, VALUABLE]** This is the most practically valuable technique in Part 8.
> DTT's fixpoint loops (`propagatePreconditionsTransitively`, `propagateFrameEffectsTransitively`)
> already compute per-function summaries. The gap is *serializing* these summaries to disk and
> invalidating only changed functions on recompilation. This is engineering work (serialize
> `FnPreconditions` + `paramWrites` maps), not a research problem. **Recommended for Tier 3.**

---

### CHC-Based Verification

#### #23: Constrained Horn Clauses (CHC)

**Source:** Bjørner 2015, Gurfinkel 2015

Encode verification as logic programming. Unified framework for many analyses.

```prolog
% Verification condition as CHC:
safe(Buf, N) :- size(Buf, S), S >= N.
error :- memcpy_call(Buf, _, N), \+ safe(Buf, N).

% Query: Is "error" derivable?
% Z3/Spacer: Returns UNSAT (safe) or SAT with counterexample
```

**Key innovations:**
- Unified encoding for safety, termination, refinement types
- Solvers: Z3 Spacer, Eldarica, μZ
- Natural loop invariant inference via fixed-point

| Aspect | Value |
|--------|-------|
| **Effort** | Medium-High |
| **Value** | **High** (unified backend) |
| **Priority** | 🟡 Medium-High |
| **Reference** | Bjørner et al., "Horn Clause Solvers for Program Verification" (2015) |

> **[REVIEW: DOESN'T FIT]** Demoted to 🟠. Elegant on paper, but DTT already has Z3 for constraint
> checking. Encoding the whole analysis as Horn clauses would replace the proof store, the visitor,
> and the annotation system with a solver-driven architecture. It's a rewrite, not an enhancement.
> The one piece worth extracting: using Z3 Spacer for loop invariant inference. That could be done
> as a targeted Z3 query at loop sites without replacing the overall architecture.

#### #24: SeaHorn

**Source:** Gurfinkel et al. 2015

LLVM-based verification via CHC. Proven on SV-COMP benchmarks.

```
Pipeline:
  C/C++ → Clang → LLVM IR → SeaHorn → CHC → Spacer
                                              ↓
                                        SAFE / UNSAFE + CEX
```

**Key innovations:**
- Works at LLVM IR level (language-independent)
- Multiple abstract domains (intervals, octagons, etc.)
- Counterexample-guided abstraction refinement

| Aspect | Value |
|--------|-------|
| **Effort** | High (LLVM integration) |
| **Value** | High |
| **Priority** | Medium |
| **Reference** | Gurfinkel et al., "The SeaHorn Verification Framework" (CAV 2015) |

---

### Separation Logic Extensions

#### #25: Incorrectness Separation Logic (ISL)

**Source:** Raad et al. 2020

Combines separation logic (heap reasoning) with incorrectness logic (bug finding).

```cpp
// ISL triple: [P] C [Q]
// "If Q holds after C, there exists an execution from P reaching Q"

// Example: prove use-after-free IS reachable
// [emp] p = malloc(10); free(p); *p = 1; [p ↦ 1]
// ISL: This state IS reachable → definite bug
```

**Key innovations:**
- Compositional bug finding (not just bug absence)
- Heap-aware path sensitivity
- Under-approximation for bug presence proofs

| Aspect | Value |
|--------|-------|
| **Effort** | High |
| **Value** | High (combines #5 with heap) |
| **Priority** | Medium |
| **Reference** | Raad et al., "Local Reasoning About the Presence of Bugs" (CAV 2020) |

#### #26: Symbolic Memory Graphs

**Source:** Kapus & Cadar 2017

Track memory structure symbolically, not just individual pointer values.

```cpp
// Traditional: buf._size = 100, p = &buf[50]
// Memory Graph:
//   Allocation(buf, 100) --contains--> Pointer(p) --offset--> 50

struct Vec { int* data; size_t len; };
Vec v;
v.data = malloc(v.len * sizeof(int));
// Graph: Alloc(id=1, size=v.len*4) ←owns← v.data
//        v.len --determines-size--> Alloc(id=1)
```

**Key innovations:**
- Captures allocation relationships
- Tracks size dependencies across fields
- Handles container patterns naturally

| Aspect | Value |
|--------|-------|
| **Effort** | High |
| **Value** | Medium (better container tracking) |
| **Priority** | Low-Medium |
| **Reference** | Kapus & Cadar, "Automatic Testing of Symbolic Executors via Program Generation and Differential Testing" (ASE 2017) |

---

### Language Extensions

#### #27: Checked C

**Source:** Elliott et al. 2018

Microsoft's gradual bounds checking extension. Compile-time checked regions.

```c
// Checked region: bounds enforced statically
void copy(_Array_ptr<char> dst : count(n),
          _Array_ptr<char> src : count(n),
          size_t n) {
    for (size_t i = 0; i < n; i++)
        dst[i] = src[i];  // Compiler verifies bounds
}

// Unchecked region: legacy code, runtime checks
void legacy(char* p, size_t n) {
    memcpy(p, "data", n);  // No static guarantee
}
```

**Key innovations:**
- Backward compatible (unchecked regions for legacy)
- Bounds in type system, not comments
- Interop between checked/unchecked

| Aspect | Value |
|--------|-------|
| **Effort** | N/A (language extension, not DTT feature) |
| **Value** | Medium (similar goals, different approach) |
| **Priority** | Low (informational) |
| **Reference** | Elliott et al., "Checked C: Making C Safe by Extension" (SecDev 2018) |

#### #28: Lifetime Inference

**Source:** Rust RFC 2094, Weiss et al. 2019

Infer ownership and lifetimes from usage patterns.

```cpp
// Inferred: return borrows from input (lifetime tied to s)
char* get_suffix(char* s) {
    return s + strlen(s) - 3;
}

// Inferred: p owns allocation, freed at scope end
void example() {
    char* p = malloc(100);
    use(p);
}  // Inferred: p should be freed here
```

**Key innovations:**
- Non-lexical lifetimes (NLL)
- Borrow checking without annotations
- Ownership transfer tracking

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | Medium-High (enhances #7 Ownership) |
| **Priority** | Medium |
| **Reference** | Weiss et al., "Oxide: The Essence of Rust" (2019) |

---

### SMT Solver Optimizations

#### #29: Incremental SMT & Theory Combination

**Source:** Cimatti 2010, Nelson-Oppen 1979

Optimize Z3 usage for repeated similar queries.

```cpp
// Incremental: reuse solver state
z3::solver s(ctx);
s.push();
s.add(buf_size >= 10);
auto r1 = s.check();     // Query 1

s.push();
s.add(n <= 5);
auto r2 = s.check();     // Query 2 (incremental, reuses Q1 state)
s.pop();

s.add(n <= 20);
auto r3 = s.check();     // Query 3 (incremental from Q1)
s.pop();
```

**Theory combination** (already in Z3):
- Arrays: `buf[i]` read/write
- Linear Integer Arithmetic: `n + offset <= size`
- Uninterpreted Functions: `hash(x) = y`
- Bitvectors: overflow checking

| Aspect | Value |
|--------|-------|
| **Effort** | Low-Medium |
| **Value** | Medium (performance) |
| **Priority** | Medium |
| **References** | Cimatti et al., "Satisfiability Modulo Theories" (2010); de Moura & Bjørner, "Z3" (TACAS 2008) |

#### #30: Quantifier Handling

**Source:** de Moura & Bjørner 2007

Handle universal quantifiers efficiently via E-matching.

```cpp
// Specification: ∀i. 0 ≤ i < n ⟹ buf[i] is valid access
// Problem: Can't enumerate all i

// E-matching: find "triggers" in formula
for (int i = 0; i < n; i++) {
    buf[i] = 0;  // Trigger: instantiate ∀ with this concrete i
}

// Z3 instantiates: buf[0] valid, buf[1] valid, ... buf[n-1] valid
```

| Aspect | Value |
|--------|-------|
| **Effort** | Low (Z3 does this) |
| **Value** | Medium |
| **Priority** | Low (DTT avoids quantifiers) |
| **Reference** | de Moura & Bjørner, "Efficient E-Matching for SMT Solvers" (CADE 2007) |

---

### Industrial Sound Analyzers

#### #31: IKOS (NASA)

**Source:** Brat et al. 2014

Abstract interpretation for safety-critical avionics code. Sound (no false negatives for covered checks).

```cpp
// IKOS checks at LLVM level:
// - Buffer overflows (array bounds)
// - Integer overflows (arithmetic)
// - Null pointer dereferences
// - Uninitialized variables
// - Division by zero

// Used for NASA flight software verification
```

**Key innovations:**
- Sound analysis (all real bugs caught)
- Low false positive rate via precise domains
- LLVM-based (works with Clang output)

| Aspect | Value |
|--------|-------|
| **Effort** | N/A (external tool comparison) |
| **Value** | High (similar goals to DTT) |
| **Priority** | Informational |
| **Reference** | Brat et al., "IKOS: A Framework for Static Analysis" (NFM 2014) |

#### #32: Frama-C/Eva

**Source:** Kirchner et al. 2015

Deductive verification with ACSL contracts. Industrial use in aerospace/automotive.

```c
/*@ requires \valid(buf + (0..n-1));
    requires n <= 100;
    assigns buf[0..n-1];
    ensures \forall integer i; 0 <= i < n ==> buf[i] == 0;
*/
void zero_buffer(char* buf, size_t n) {
    /*@ loop invariant 0 <= i <= n;
        loop invariant \forall integer j; 0 <= j < i ==> buf[j] == 0;
        loop assigns i, buf[0..n-1];
        loop variant n - i;
    */
    for (size_t i = 0; i < n; i++)
        buf[i] = 0;
}
```

**Key innovations:**
- ACSL specification language
- WP (Weakest Precondition) plugin for proof
- Eva plugin for value analysis
- Combines multiple analysis techniques

| Aspect | Value |
|--------|-------|
| **Effort** | N/A (external tool comparison) |
| **Value** | High (DTT annotations inspired by ACSL) |
| **Priority** | Informational |
| **Reference** | Kirchner et al., "Frama-C: A Software Analysis Perspective" (SEFM 2015) |

---

### Declarative Analysis

#### #33: CodeQL / Semgrep

**Source:** Avgustinov et al. 2016

Query-based code analysis. Patterns expressed declaratively.

```sql
-- CodeQL: Find memcpy where size comes from untrusted source
from FunctionCall memcpy, DataFlow::Node source, DataFlow::Node sink
where memcpy.getTarget().hasName("memcpy")
  and sink.asExpr() = memcpy.getArgument(2)
  and source.asExpr() instanceof NetworkReadExpr
  and DataFlow::localFlow(source, sink)
select memcpy, "Tainted size flows to memcpy"
```

```yaml
# Semgrep: Pattern-based matching
rules:
  - id: unsafe-memcpy
    pattern: memcpy($DST, $SRC, $SIZE)
    message: "memcpy with unvalidated size"
```

**Key innovations:**
- Declarative pattern specification
- Dataflow tracking via queries
- Large rule libraries (community/commercial)

| Aspect | Value |
|--------|-------|
| **Effort** | Low-Medium (export DTT results as queries) |
| **Value** | Medium |
| **Priority** | Low |
| **Reference** | Avgustinov et al., "QL: Object-oriented Queries on Relational Data" (ECOOP 2016) |

---

### Part 8 Priority Summary (Original)

| Priority | Techniques |
|----------|------------|
| 🔴 **High** | #21 Pulse (compositional), #22 Incremental |
| 🟡 **Medium-High** | #23 CHC |
| ⚪ **Medium** | #24 SeaHorn, #25 ISL, #28 Lifetime, #29 Inc. SMT |
| ⚫ **Low/Info** | #26 MemGraphs, #27 Checked C, #30 Quantifiers, #31-33 (tools) |

### Part 8 Priority Summary (Revised)

> **[REVIEW]** #21 and #23 demoted due to architectural mismatch. #22 and #29 promoted —
> they work within the existing architecture and address real scalability concerns.

| Priority | Techniques | Rationale |
|----------|------------|-----------|
| 🔴 **Fits, High Value** | #22 Incremental, #29 Inc. SMT | Work within current architecture |
| 🟡 **Valuable Feature** | #28 Lifetime | Enhances existing move/resource tracking |
| 🟠 **Doesn't Fit** | #21 Pulse, #23 CHC | Require separation logic or engine replacement |
| ⚪ **Medium** | #24 SeaHorn, #25 ISL | Interesting but external tools, not DTT enhancements |
| ⚫ **Low/Info** | #26 MemGraphs, #27 Checked C, #30 Quantifiers, #31-33 (tools) | Unchanged |

### Part 8 Recommended Next Steps (Revised)

1. **#29 Incremental SMT** - Mechanical change (~100 lines): use `push()`/`pop()` instead of fresh Z3 contexts
2. **#22 Incremental Analysis** - Serialize `FnPreconditions` + `paramWrites` maps to disk
3. **#28 Lifetime Inference** - Extend ghost field tracking with borrow/ownership semantics
4. ~~#21 Pulse~~ — deferred: requires separation logic engine
5. ~~#23 CHC~~ — deferred: requires engine replacement

---

## Part 9: Advanced Loop & Scalability Techniques (34-43)

Additional techniques focused on loop analysis precision and analysis scalability.

> **[REVIEW]** Part 9 has the widest spread between "fits DTT" and "doesn't fit DTT." Two techniques
> (#38 Sparse, #39 k-CFA) are high-value and work within the AST visitor. Three (#34 k-Induction,
> #35 Recurrence full, #40 PDR) require loop unrolling or CFG-level reasoning that the visitor
> doesn't support. #42 TypeState is already implemented via ghost fields. #43 Value Range is
> already partial.

### Loop Analysis

#### #34: k-Induction

**Source:** Sheeran et al. 2000, Donaldson et al. 2011

Prove loop safety without discovering full invariant. Check base cases + inductive step for k iterations.

```cpp
// k-Induction (k=2):
// Base: iterations 0,1 safe
// Step: if iterations i,i+1 safe, then i+2 safe

for (size_t i = 0; i < n; i++) {
    buf[i] = 0;
}
// k=1 fails (need to know i < n)
// k=2 succeeds (inductive step proves i stays bounded)
```

**Key insight:** Stronger than BMC, weaker than full invariant synthesis. Often sufficient for buffer bounds.

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | **High** (loop bounds without full invariant) |
| **Priority** | 🔴 High |
| **Reference** | Donaldson et al., "Software Verification Using k-Induction" (SAS 2011) |

> **[REVIEW: DOESN'T FIT]** Demoted to 🟠. k-Induction requires unrolling the loop body k times
> and checking the inductive step. DTT's AST visitor traverses the loop body exactly once. You'd
> need to re-traverse the body k times with different state, which means either building a CFG or
> creating a "replay" mechanism for the visitor. For the common `for(i=0;i<n;i++)` pattern, DTT
> already extracts the bound directly — k-induction solves a problem DTT doesn't have for simple
> loops, and is too expensive for the complex loops where it would help.

#### #35: Recurrence Solving

**Source:** Farzan & Kincaid 2015, Kincaid et al. 2017

Compute closed-form solutions for loop variable evolution.

```cpp
for (size_t i = 0; i < n; i++) {
    buf[i] = 0;
}
// Recurrence: i' = i + 1, i_0 = 0
// Closed form: i_k = k
// At exit: i = n
// Precondition: buf._size >= n
```

**Key insight:** Avoids iterative fixpoint; directly computes final/intermediate values.

| Aspect | Value |
|--------|-------|
| **Effort** | Medium-High |
| **Value** | **High** (precise loop analysis) |
| **Priority** | 🔴 High |
| **Reference** | Kincaid et al., "Compositional Recurrence Analysis" (FMCAD 2017) |

> **[REVIEW: PARTIALLY FITS]** The full recurrence solving framework is heavy, but the *simplified*
> version — "record `i == n` after `for(i=0;i<n;i++)`" — is ~50 lines and already listed as
> Part 0.2. That captures the closed-form solution for the trivial recurrence `i' = i + 1, i_0 = 0`
> without any framework. For step values > 1 (`i += 2`), DTT already tracks the step in
> `TraverseForStmt`. Recording `i == bound` at loop exit handles 90% of the practical need.
> Full recurrence solving (polynomial recurrences, nested loops) is Tier 4.

#### #36: Loop Acceleration

**Source:** Kroening et al. 2008, Jeannet et al. 2014

Compute the effect of multiple loop iterations in one step.

```cpp
// Original:
for (int i = 0; i < n; i++) { sum += arr[i]; }

// Accelerated: compute effect of k iterations at once
// Effect of k iterations: i += k, accesses arr[0..k-1]
// Jump to: i = n, accesses arr[0..n-1]
// Precondition: arr._size >= n
```

**Key insight:** Avoids iteration-by-iteration analysis; computes transitive closure.

| Aspect | Value |
|--------|-------|
| **Effort** | High |
| **Value** | High (complex loops) |
| **Priority** | Medium |
| **Reference** | Kroening et al., "Under-Approximating Loops in C Programs" (TACAS 2008) |

#### #37: Numeric Invariant Templates

**Source:** Colón et al. 2003, Sankaranarayanan et al. 2004

Generate invariants from parameterized templates via constraint solving.

```cpp
// Template: a*i + b*n + c >= 0 (find a, b, c)
// Constraint solving finds coefficients
// Result: i <= n (invariant for loop)

for (size_t i = 0; i < n; i++) {
    buf[i] = 0;  // Template proves: 0 <= i < n
}
```

**Key insight:** Specify invariant shape, solver finds coefficients.

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | High (automatic invariants) |
| **Priority** | 🟡 Medium-High |
| **Reference** | Colón et al., "Linear Invariant Generation Using Non-linear Constraint Solving" (CAV 2003) |

---

### Scalability & Precision

#### #38: Sparse Analysis

**Source:** Oh et al. 2012, Hardekopf & Lin 2011

Only track variables that affect the property being checked.

```cpp
void f(char* buf, size_t n, int x, int y, int z) {
    // Sparse: only track buf, n (affect memcpy safety)
    // Skip: x, y, z (irrelevant to buffer bounds)
    int a = x + y;  // SKIP - irrelevant
    int b = y * z;  // SKIP - irrelevant
    memcpy(buf, "data", n);  // TRACK: buf, n
}
```

**Key insight:** Relevance analysis + selective tracking = faster analysis.

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | **Very High** (performance) |
| **Priority** | 🔴 High |
| **Reference** | Oh et al., "Design and Implementation of Sparse Global Analyses" (PLDI 2012) |

> **[REVIEW: FITS, HIGHEST SCALABILITY VALUE]** Rating confirmed. This is the single highest-value
> scalability improvement that works within DTT's architecture. DTT currently updates `TypedEnv`
> for every variable assignment in every function. A pre-pass that identifies which variables flow
> into checked operations (buffer accesses, null derefs, resource ops) would let the main visitor
> skip irrelevant assignments entirely. Implementation: before `TraverseDecl`, walk the function
> body to collect variable names used in `operator[]`, `memcpy`, pointer deref, etc., then gate
> `TypedEnv` updates on membership in this set. ~500-800 lines. This is the difference between
> "works on 5K-line files" and "works on 50K-line files." **Recommended for Tier 2.**

#### #39: Context-Sensitive Analysis (k-CFA)

**Source:** Shivers 1991, Smaragdakis & Bravenboer 2011

Distinguish different call contexts for the same function.

```cpp
void copy(char* dst, size_t n) { memcpy(dst, "x", n); }

void caller1() {
    char buf[10];
    copy(buf, 10);  // Context 1: buf._size=10, n=10 → SAFE
}

void caller2() {
    char buf[5];
    copy(buf, 10);  // Context 2: buf._size=5, n=10 → UNSAFE
}

// Context-insensitive: merges both → loses precision
// k-CFA (k=1): separates → finds bug only in caller2
```

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | **High** (precision) |
| **Priority** | 🔴 High |
| **Reference** | Smaragdakis & Bravenboer, "Pick Your Contexts Well" (POPL 2011) |

> **[REVIEW: FITS, HIGH PRECISION VALUE]** Rating confirmed. DTT's `FnPreconditions` map stores
> one summary per `FunctionDecl*`. If `copy(buf, n)` is called from two sites with different buffer
> sizes, the summary merges them. Keying summaries by `(FunctionDecl*, CallExpr*)` — i.e., one
> summary per call site — would let DTT distinguish safe calls from unsafe ones. This is 1-CFA
> (k=1), the simplest form. Implementation: change `FnPreconditions` key from `FunctionDecl*` to
> `std::pair<FunctionDecl*, SourceLocation>`, clone the precondition check per call site. ~400 lines.
> **Recommended for Tier 2.**

#### #40: Property-Directed Reachability (PDR/IC3)

**Source:** Bradley 2011, Cimatti & Griggio 2012

Efficient algorithm for safety property checking. Learns lemmas lazily on-demand.

```cpp
// Property: no buffer overflow
// PDR iteratively strengthens invariant:
// Frame 0: TRUE
// Frame 1: buf._size >= n (learned from counterexample)
// Frame 2: buf._size >= n ∧ i <= n
// ...until inductive

for (size_t i = 0; i < n; i++) {
    buf[i] = 0;
}
// PDR finds: buf._size >= n is inductive
```

**Key insight:** On-demand invariant learning; often faster than abstract interpretation.

| Aspect | Value |
|--------|-------|
| **Effort** | High |
| **Value** | **Very High** (state-of-the-art for safety) |
| **Priority** | 🔴 High |
| **Reference** | Bradley, "SAT-Based Model Checking without Unrolling" (VMCAI 2011) |

> **[REVIEW: DOESN'T FIT]** Demoted to 🟠. PDR requires a transition system representation where
> states are sets of variable valuations and transitions are program statements. DTT's proof store
> is a fact database, not a state space. PDR also needs to enumerate predecessors of error states
> (backward reachability), which the forward visitor can't do. This is "build a model checker"
> rather than "enhance DTT." The document rates this "Very High" based on academic results, but
> those results apply to hardware verification and small programs, not to a Clang plugin analyzing
> C++ templates and STL types.

#### #41: Datalog-Based Analysis

**Source:** Whaley & Lam 2004, Scholz et al. 2016 (Soufflé)

Express analysis as Datalog rules; engine computes fixpoint efficiently.

```prolog
% Datalog rules for buffer safety:
bufferSize(Buf, Size) :- allocation(Buf, Size).
bufferSize(Ptr, Size) :- assign(Ptr, Buf), bufferSize(Buf, Size).

unsafe(Call) :-
    memcpyCall(Call, Buf, _, N),
    bufferSize(Buf, Size),
    N > Size.
```

**Key insight:** Declarative specification; optimized Datalog engines very fast.

| Aspect | Value |
|--------|-------|
| **Effort** | Medium |
| **Value** | High (scalability + declarative) |
| **Priority** | Medium |
| **Reference** | Scholz et al., "On Fast Large-Scale Program Analysis in Datalog" (CC 2016) |

---

### State Tracking

#### #42: Type State Analysis

**Source:** Strom & Yemini 1986, Fink et al. 2008

Track object state transitions (e.g., allocated → initialized → freed).

```cpp
// Type states for buffer:
// UNALLOCATED → ALLOCATED → INITIALIZED → FREED

char* buf;                    // State: UNALLOCATED
buf = malloc(100);            // State: ALLOCATED
memset(buf, 0, 100);          // State: INITIALIZED
use(buf);                     // OK: INITIALIZED
free(buf);                    // State: FREED
use(buf);                     // ERROR: FREED state
```

**Key insight:** Finite state machine per object; lightweight tracking.

| Aspect | Value |
|--------|-------|
| **Effort** | Low-Medium |
| **Value** | High (use-after-free, double-free) |
| **Priority** | 🟡 Medium-High |
| **Reference** | Fink et al., "Effective Typestate Verification in the Presence of Aliasing" (TOSEM 2008) |

> **[REVIEW: ALREADY IMPLEMENTED]** DTT already does typestate analysis via ghost fields. The
> `@open`, `@allocated`, `@fd` ghost fields in `.dtt` annotations are exactly finite state machines:
> `UNALLOCATED → ALLOCATED → FREED`. `PointerLifetime` enum (`Valid`, `Freed`, `Unknown`) tracks
> use-after-free. `scope_exit` constraints enforce resource cleanup. What's *not* implemented:
> user-definable state machines in `.dtt` format (e.g., `states: INIT, CONNECTED, CLOSED`). That
> would be an annotation parser feature (~200 lines), not an analysis engine change.

#### #43: Value Range Propagation

**Source:** Harrison 1977, Patterson 1995

Efficiently track integer ranges through the program.

```cpp
void f(unsigned char idx, int arr[256]) {
    // Range: idx ∈ [0, 255] (from type)
    arr[idx] = 0;  // Safe: 256 > 255
}

void g(size_t n, char* buf) {
    if (n > 100) return;
    // Range: n ∈ [0, 100]
    if (n < 10) return;
    // Range: n ∈ [10, 100]
    memcpy(buf, "data", n);  // Need: buf._size >= 100
}
```

**Key insight:** Lightweight (no SMT), handles common cases.

| Aspect | Value |
|--------|-------|
| **Effort** | Low |
| **Value** | High (fast, handles common cases) |
| **Priority** | 🟢 Already partial in DTT |
| **Reference** | Patterson, "Accurate Static Branch Prediction by Value Range Propagation" (PLDI 1995) |

---

### Part 9 Priority Summary (Original)

| Priority | Techniques |
|----------|------------|
| 🔴 **High** | #34 k-Induction, #35 Recurrence, #38 Sparse, #39 k-CFA, #40 PDR |
| 🟡 **Medium-High** | #37 Templates, #42 TypeState |
| 🟢 **Already Partial** | #43 Value Range |
| ⚪ **Medium** | #36 Acceleration, #41 Datalog |

### Part 9 Priority Summary (Revised)

> **[REVIEW]** Three of the five "High" techniques don't fit DTT's architecture. Two do.

| Priority | Techniques | Rationale |
|----------|------------|-----------|
| 🔴 **Fits, High Value** | #38 Sparse, #39 k-CFA | Work within AST visitor, address real needs |
| 🟢 **Already Implemented** | #42 TypeState (ghost fields), #43 Value Range (partial) | Extend, don't build |
| 🟡 **Partially Fits** | #35 Recurrence (simplified: Part 0.2), #37 Templates | Simplified versions valuable |
| 🟠 **Doesn't Fit** | #34 k-Induction, #40 PDR | Require loop unrolling / backward reachability |
| ⚪ **Medium** | #36 Acceleration, #41 Datalog | Unchanged |

### Part 9 Recommended Next Steps (Revised)

1. **#38 Sparse Analysis** - Immediate performance win; only track relevant variables
2. **#39 k-CFA** - Better precision for call-site checking (keyed summaries)
3. **#35 Recurrence (simplified)** - Post-loop values via Part 0.2 (~50 lines)
4. ~~#40 PDR/IC3~~ — deferred: requires transition system / backward reachability
5. ~~#34 k-Induction~~ — deferred: requires loop body re-traversal

---

## Part 10: Revised Action Plan (NEW)

> **[REVIEW]** This replaces the original Part 4 schedule. Prioritized by value-to-effort ratio
> and architectural fit.

### Phase 1: Practical Gap Closures (1-2 weeks)

Do these first. They close real user-visible bugs and are all testable in isolation.

| # | Item | Lines | Value | Test |
|---|------|-------|-------|------|
| 1 | Else-branch negation (Part 0.1 / Phase 5A) | ~150 | Eliminates class of false positives | `970_path_else_negation_pass.cpp` |
| 2 | Post-loop variable values (Part 0.2) | ~50 | Enables post-loop reasoning | `960_postloop_value_pass.cpp` |
| 3 | Value range narrowing from guards (Part 0.3) | ~200 | Handles `if (n > 100) return;` | `971_guard_narrowing_pass.cpp` |
| 4 | Ternary expression tracking (Part 0.4) | ~100 | Handles `cond ? a : b` | `963_ternary_tracking_pass.cpp` |

### Phase 2: Scalability & Precision (2-4 weeks)

These improve DTT's ability to handle real-world code at scale.

| # | Item | Lines | Value | Technique |
|---|------|-------|-------|-----------|
| 5 | Sparse analysis pre-pass (#38) | ~500-800 | Performance on large TUs | Skip irrelevant variables |
| 6 | Context-sensitive summaries (#39) | ~400 | Precision at call sites | Key FnPreconditions by call site |
| 7 | Incremental Z3 (#29) | ~100 | Solver performance | `push()`/`pop()` instead of fresh contexts |

### Phase 3: New Capabilities (3-6 weeks, pick based on demand)

These add genuinely new detection abilities.

| # | Item | Effort | Value | Technique |
|---|------|--------|-------|-----------|
| 8 | Range-for loop bounds (Part 0.5) | ~150 lines | Modern C++ support | AST pattern match |
| 9 | Switch-case variant proofs (Part 0.6) | ~200 lines | Variant safety | AST pattern match |
| 10 | Summary serialization (#22) | 1-2 weeks | CI performance | Serialize FnPreconditions to disk |
| 11 | Taint analysis (#19) | 2-3 weeks | Heartbleed-class bugs | See TAINT_TRACKING_PROPOSAL.md |
| 12 | Path condition accumulation (Phase 5B) | 1-2 weeks | Better diagnostics | Track branch conditions |

### Phase 4: Advanced (Only If Architecture Evolves)

Do not attempt these without a CFG. They require fundamental changes to DTT's analysis model.

| # | Item | Prerequisite | Technique |
|---|------|-------------|-----------|
| 13 | Widening/Narrowing (full Part 1) | CFG or loop body replay | Abstract interpretation |
| 14 | Feasibility checking (Phase 5C) | Phase 5B complete | Z3 per-branch |
| 15 | Join-point merging (Phase 5D) | Lattice-based proof store | State merging |
| 16 | k-Induction (#34) | CFG or loop body replay | Inductive step checking |
| 17 | PDR/IC3 (#40) | Transition system model | Backward reachability |
| 18 | Pulse-style compositionality (#21) | Separation logic engine | Heap reasoning |
| 19 | CHC encoding (#23) | Replace proof store | Solver-driven analysis |
| 20 | CEGAR (#13) | Re-analysis loop | Refinement iteration |

### Key Principle

**Do the boring work first.** The 43 academic techniques in this document are intellectually
interesting, but the features that close the most real gaps are AST pattern-matching: detect a
code pattern in the visitor, add a proof to the store, remove it when scope exits. ~50-200 lines
each. The literature survey is a useful reference for when DTT evolves beyond the single-pass
visitor — it is not a roadmap for what to build next.

---

## Part 11: Recent Literature & Industry Developments (2022-2026) (NEW)

> **[REVIEW 2026-02-23]** Fresh survey of academic venues (PLDI, OOPSLA, CAV, ISSTA, ASE, FSE,
> ECOOP) and industry tools from 2022-2026. The most impactful finding: the highest-ROI
> improvements for DTT are not analysis algorithm changes but **ecosystem integrations** (C++26
> contracts, SARIF output, cross-TU summaries) and **LLM-assisted augmentation** (invariant
> inference, annotation generation, false-positive filtering). These work with DTT's existing
> single-pass visitor architecture.

### 11.1 C++ Standards & Language Evolution

#### #44: C++26 Contracts (P2900)

**Source:** P2900R6 (core proposal), P2899R1 (rationale). Adopted Hagenberg June 2025, final
approval expected March 2026.

C++26 standardizes `pre(condition)` and `post(condition)` on function declarations:

```cpp
int safe_access(int* arr, size_t n, size_t idx)
    pre(arr != nullptr)
    pre(idx < n)
    post(r: r >= 0)
{
    return arr[idx];
}
```

**Fit for DTT: HIGH.** DTT already has its own `.dtt` annotation language for
preconditions/postconditions. When C++26 contracts ship in Clang, DTT could parse `pre`/`post`
attributes directly from the AST — `ContractAttr` nodes on `FunctionDecl`. This would let users
annotate their own code in standard C++ while DTT continues using `.dtt` for unannotated
third-party libraries. The single-pass visitor is ideal since contracts live on declarations.

| Aspect | Value |
|--------|-------|
| **Effort** | Medium (2-4 weeks once Clang 20+ ships contract AST support) |
| **Value** | **Very High** (standard annotation syntax, adoption enabler) |
| **Priority** | 🔴 High (but blocked on Clang implementation) |
| **References** | [P2900R6](https://isocpp.org/files/papers/P2900R6.pdf), [P2899R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2899r1.pdf) |

#### #45: C++ Safety Profiles (P3081, P3274, P3465, P3651)

**Source:** Stroustrup P3274R0/P3651R0 (framework), P3081R2 (core profiles), P3465 (lifetime TS).

Framework for standardized static analysis profiles — bounds, type, lifetime — enforceable by
compilers. Contentious but actively pursued.

**Fit for DTT: HIGH.** DTT is already a working prototype of what Profiles aims to standardize:
- Bounds profile → `operator[]` requires `arg0 < _size`
- Type profile → `_variant_index` checking, `_has_value` checking
- Lifetime profile → `PointerLifetime::Freed` tracking, `@open`/`@allocated` ghost fields

DTT could add a `--dtt-profile=bounds` mode that enables only bounds-related checks, matching
P3081. The checks exist; the work is organizing them under profile names.

| Aspect | Value |
|--------|-------|
| **Effort** | Low-Medium (2-3 weeks to add profile modes) |
| **Value** | **High** (strategic positioning — DTT implements Profiles today) |
| **Priority** | 🔴 High |
| **References** | [P3081R2](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3081r2.pdf), [P3274R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3274r0.pdf), [P3651R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3651r0.pdf) |

#### #46: Apple `-fbounds-safety` Extension

**Source:** Clang extension being upstreamed since 2024. Deployed on production Apple OS code.

Enforces bounds safety for C via annotations like `__counted_by(N)` on pointer parameters.

```c
void fill(int *__counted_by(n) buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = 0;
}
```

**Fit for DTT: MEDIUM.** The `__counted_by` model is close to DTT's `_size` tracking. DTT could
consume these annotations as `_size` postconditions. C-only (no C++ yet), focuses on runtime
traps rather than compile-time proof, but the annotation semantics align.

| Aspect | Value |
|--------|-------|
| **Effort** | Low (1-2 weeks — read attributes from AST as `_size` facts) |
| **Value** | Medium (interop with Apple ecosystem) |
| **Priority** | 🟡 Medium |
| **References** | [Apple sample adoption](https://github.com/apple/sample-fbounds-safety-adoption), [Clang docs](https://rocm.docs.amd.com/projects/llvm-project/en/latest/LLVM/clang/html/BoundsSafety.html) |

---

### 11.2 LLM-Integrated Static Analysis

#### #47: LLM Post-Filter for False Positive Reduction

**Source:** LLift (OOPSLA 2024), IRIS (2024), LLM-generated SMT queries (2025).

Use LLMs as a post-processing filter on static analysis warnings to distinguish true from false
positives. LLift found 4 previously unknown Linux kernel bugs by having GPT-4 reason about
use-before-initialization patterns that confused traditional analysis.

**Fit for DTT: MEDIUM-HIGH (as a post-processing step).** DTT has well-defined diagnostic
categories (A-H). An LLM could filter Category C (lost tracking) and Category D (solver failed)
warnings by presenting the code context, proof store state, and failed Z3 query. Does not touch
the single-pass visitor — operates on DTT's output.

| Aspect | Value |
|--------|-------|
| **Effort** | Medium (3-6 weeks for prototype) |
| **Value** | **High** (reduces false positives without analysis changes) |
| **Priority** | 🟡 Medium-High (latency/cost concerns for interactive use) |
| **References** | [LLift (OOPSLA 2024)](https://2024.splashcon.org/details/splash-2024-oopsla/18/), [IRIS](https://arxiv.org/abs/2405.17238), [LLM SMT queries (2025)](https://arxiv.org/pdf/2506.10322) |

#### #48: LLM-Assisted Loop Invariant Inference

**Source:** "LLM Meets BMC" (ASE 2024), "Hybrid LLMs + SMT" (2025).

LLM generates candidate loop invariants; SMT solver verifies them. ASE 2024 "query-filter-
reassemble" strategy solved 309/316 benchmarks.

**Fit for DTT: HIGH (architecturally compatible).** DTT's biggest loop weakness is the single-pass
visitor seeing loop bodies once. An LLM pre-pass could: scan for loops, propose invariants (e.g.,
"after the loop, `v._size >= n`"), inject as synthetic proofs into DTT's store. The Z3
verification step uses exactly the queries DTT already generates.

```
Pipeline:
  Loop source text → LLM → Candidate invariant → Z3 verify → Inject into ProofStore
```

| Aspect | Value |
|--------|-------|
| **Effort** | High (4-8 weeks) |
| **Value** | **High** (closes loop analysis limitation without CFG) |
| **Priority** | 🟡 Medium-High (batch/CI-time, not compile-time) |
| **References** | [LLM Meets BMC (ASE 2024)](https://dl.acm.org/doi/10.1145/3691620.3695014), [Hybrid LLMs+SMT (2025)](https://arxiv.org/html/2508.00419) |

#### #49: LLM-Assisted Annotation Generation

**Source:** "Enchanting Specification Synthesis" (CAV 2024), AdaTaint (2025).

LLMs generate function specifications (preconditions/postconditions) from source code, validated
by static analysis or SMT backends.

**Fit for DTT: HIGH.** The biggest practical barrier to DTT adoption is writing `.dtt` files for
project-specific APIs. An LLM could: (1) identify un-annotated functions called with tracked
arguments, (2) read the source, (3) propose `.dtt` annotations, (4) DTT validates against test
cases via Z3. DTT already auto-infers postconditions (`InferredPostcondition`) — LLM generation
would cover functions where auto-inference fails.

| Aspect | Value |
|--------|-------|
| **Effort** | Medium (3-4 weeks) |
| **Value** | **Very High** (annotation coverage is the adoption bottleneck) |
| **Priority** | 🔴 High |
| **References** | [LLM Spec Synthesis (CAV 2024)](https://link.springer.com/chapter/10.1007/978-3-031-65630-9_16), [AdaTaint (2025)](https://arxiv.org/html/2511.04023) |

---

### 11.3 Ecosystem & Integration

#### #50: SARIF Output Format

**Source:** OASIS SARIF 2.1.0 standard. Supported by GitHub Code Scanning, VS Code, SonarQube.

**📋 See:** [`docs/plans/SARIF_OUTPUT_PLAN.md`](SARIF_OUTPUT_PLAN.md) for existing design.

Standardized JSON format for static analysis results. Enables GitHub PR annotations, diff-based
CI (compare SARIF between base and PR for new-only findings), and tool interoperability.

| Aspect | Value |
|--------|-------|
| **Effort** | Medium (2-3 weeks — existing plan) |
| **Value** | **High** (CI integration, PR annotations) |
| **Priority** | 🔴 High |
| **Reference** | [SARIF 2.1.0](https://docs.oasis-open.org/sarif/sarif/v2.1.0/sarif-v2.1.0.html) |

#### #51: Cross-TU Analysis via Serialized Summaries

**Source:** Clang CTU analysis, Infer modular analysis, Paderborn scaling dissertation (2024).

Serialize per-function summaries (`InferredPrecondition`, `InferredPostcondition`, `paramWrites`)
for consumption in other translation units.

**Fit for DTT: HIGH.** DTT's fixpoint loops already compute the summaries. The gap is
serialization and loading. Format: JSON keyed by mangled function name. Load during annotation
phase alongside `.dtt` files.

| Aspect | Value |
|--------|-------|
| **Effort** | Medium (3-4 weeks) |
| **Value** | **Very High** (removes single-TU limitation) |
| **Priority** | 🔴 High |
| **References** | [Clang CTU](https://ieeexplore.ieee.org/document/8530731/), [Scaling IFDS/IDE (ECOOP 2024)](https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2024.36) |

---

### 11.4 Solver & Reliability

#### #52: Z3 Determinism (Mariposa)

**Source:** CMU 2024. The "butterfly effect" in SMT-based verification: tiny source changes can
flip solver outcomes.

**Fit for DTT: MEDIUM.** DTT could flip between "safe" (Cat E) and "solver failed" (Cat D) across
compilations if Z3 is non-deterministic. Fix: set `z3::context` parameters for deterministic
behavior, canonicalize queries (sort conjuncts, normalize variable names), add retry for timeouts.

| Aspect | Value |
|--------|-------|
| **Effort** | Low (a few days) |
| **Value** | Medium (reliability of Cat D/E classification) |
| **Priority** | 🟡 Medium |
| **Reference** | [Mariposa (CMU 2024)](https://www.cs.cmu.edu/~csd-phd-blog/2024/mariposa/) |

#### #53: Annotation Fault Detection

**Source:** FSE 2024 study of 246 bugs in 6 static analyzers caused by incorrect annotations.

DTT's `.dtt` files are its own source of truth. Wrong annotations cause false positives. Common
fault patterns: missing edge cases, incorrect polarity, stale annotations.

| Aspect | Value |
|--------|-------|
| **Effort** | Low (1 week — systematic annotation validation tests) |
| **Value** | Medium (correctness assurance) |
| **Priority** | 🟡 Medium |
| **Reference** | [Annotation-Induced Faults (FSE 2024)](https://dl.acm.org/doi/10.1145/3643759) |

---

### 11.5 Related Tools (2022-2026)

#### #54: Coconut — Typestate for C++ (2024-2025)

**Source:** ScienceDirect 2025, COORDINATION 2024. GCC GIMPLE-based typestate checker.

Validates DTT's ghost-field approach for C++ typestate. Key difference: Coconut requires
template-based state encoding in user code; DTT uses external `.dtt` annotations (more
practical for existing code). Coconut's aliasing handling is worth studying for DTT's
ghost-field tracking through pointer indirection.

| Aspect | Value |
|--------|-------|
| **Effort** | N/A (conceptual, not code adoption) |
| **Value** | Medium (validates DTT's approach, informs alias handling) |
| **Priority** | Informational |
| **References** | [Coconut (ScienceDirect 2025)](https://www.sciencedirect.com/science/article/pii/S0167642325001376), [COORDINATION 2024](https://link.springer.com/chapter/10.1007/978-3-031-62697-5_12) |

---

### Part 11 Priority Summary

| Priority | Technique | Why |
|----------|-----------|-----|
| 🔴 **Do Now/Soon** | #45 Safety Profiles alignment, #50 SARIF output, #51 Cross-TU summaries | Strategic positioning + adoption enablers |
| 🔴 **Do When Clang Ships** | #44 C++26 Contracts | Blocked on Clang 20+ contract AST support |
| 🟡 **High Value, Medium Effort** | #49 LLM annotation generation, #47 LLM FP filtering, #48 LLM loop invariants | Biggest precision wins without architecture change |
| 🟡 **Medium** | #46 `-fbounds-safety` interop, #52 Z3 determinism, #53 Annotation fault testing | Ecosystem + reliability |
| ⚪ **Informational** | #54 Coconut | Validates approach, informs future work |

### Key Takeaway

The original catalog (Parts 7-9) focused on analysis algorithms (widening, CEGAR, PDR, CHC).
The 2022-2026 landscape has shifted: the highest-ROI improvements are **ecosystem integrations**
(C++26 contracts, SARIF, cross-TU summaries, Safety Profiles) and **LLM augmentation** (annotation
generation, false-positive filtering, loop invariant inference). These work *with* DTT's
single-pass visitor architecture rather than requiring it to be replaced.

DTT's architecture — a forward-driven AST visitor with declarative annotations and Z3 backend —
turns out to be a pragmatic sweet spot relative to the 2024-2026 state of the art. The tools
that are scaling (Infer/Pulse, PhASAR) use fundamentally different architectures (separation
logic, IFDS/IDE on LLVM IR). DTT should not try to become them. Instead, it should lean into its
strengths: lightweight, annotation-driven, zero-runtime-overhead checking that integrates with
the C++ ecosystem as it evolves toward contracts and profiles.

---

## Appendix A: Key Code Locations

| Component | File | Lines |
|-----------|------|-------|
| Current loop handling | `src/DTTPlugin.cpp` | 8011-8467 |
| Guard extraction | `src/DTTPlugin.cpp` | 7137-7265 |
| IfStmt traversal | `src/DTTPlugin.cpp` | 7687-8000 |
| ProofContext | `src/DTTPlugin.cpp` | 627-767 |
| VarBoundProof | `src/DTTPlugin.cpp` | 2557-2610 |
| Z3 integration | `src/DTTPlugin.cpp` | Throughout |
| SymValue | `src/DTTPlugin.cpp` | 976-1047 |
| ProofOp enum | `src/DTTTypes.h` | 25-26 |

## Appendix B: References

### Core Techniques (Parts 1-2)
1. Cousot, P. & Cousot, R. (1977). "Abstract Interpretation: A Unified Lattice Model"
2. O'Hearn, P. (2019). "Incorrectness Logic" - POPL 2019
3. Calcagno, C. et al. (2011). "Compositional Shape Analysis by Means of Bi-Abduction"

### Alias Analysis (#2)
4. Andersen, L.O. (1994). "Program Analysis and Specialization for the C Programming Language"
5. Steensgaard, B. (1996). "Points-to Analysis in Almost Linear Time"

### Gradual Types (#6)
6. Siek, J. & Taha, W. (2006). "Gradual Typing for Functional Languages"
7. Lehmann, N. & Tanter, É. (2017). "Gradual Refinement Types"

### Separation Logic (#7)
8. Reynolds, J.C. (2002). "Separation Logic: A Logic for Shared Mutable Data Structures"

### Relational Analysis (#8, #16)
9. Cousot, P. & Halbwachs, N. (1978). "Automatic Discovery of Linear Restraints"
10. Miné, A. (2006). "The Octagon Abstract Domain"

### Demand-Driven (#11)
11. Horwitz, S. & Reps, T. (1995). "Demand Interprocedural Dataflow Analysis"

### Predicate Abstraction (#12)
12. Ball, T. & Rajamani, S.K. (2001). "The SLAM Project: Debugging System Software via Static Analysis"
13. Henzinger, T.A. et al. (2002). "Lazy Abstraction" - POPL 2002

### CEGAR (#13)
14. McMillan, K.L. (2003). "Interpolation and SAT-Based Model Checking"

### Liquid Types (#14)
15. Rondon, P., Kawaguchi, M., Jhala, R. (2008). "Liquid Types" - PLDI 2008

### Summaries (#15)
16. Reps, T., Horwitz, S., Sagiv, M. (1995). "Precise Interprocedural Dataflow Analysis via Graph Reachability"

### Symbolic Execution (#17)
17. Kuznetsov, V. et al. (2012). "Efficient State Merging in Symbolic Execution"

### Bounded Model Checking (#18)
18. Clarke, E., Kroening, D., Lerda, F. (2004). "A Tool for Checking ANSI-C Programs" - TACAS 2004

### Taint Analysis (#19)
19. Livshits, V.B. & Lam, M.S. (2005). "Finding Security Vulnerabilities in Java Applications with Static Analysis"

### ACDL (#20)
20. D'Silva, V., Haller, L., Kroening, D. (2014). "Abstract Conflict Driven Learning"

### Compositional Analysis (#21, #22)
21. Calcagno, C. et al. (2015). "Moving Fast with Software Verification" - ICSE 2015
22. Distefano, D. et al. (2019). "Scaling Static Analyses at Facebook" - CACM
23. Arzt, S. et al. (2014). "Reviser: Efficiently Updating IDE-/IFDS-Based Data-Flow Analyses" - ISSTA 2014
24. Szabó, T. et al. (2021). "Incrementalizing Lattice-Based Analyses" - OOPSLA 2021
25. Cousot, P. (2019). "A Scalable Segmented Decision Tree Abstract Domain"

### CHC & Verification (#23, #24)
26. Bjørner, N. et al. (2015). "Horn Clause Solvers for Program Verification"
27. Gurfinkel, A. et al. (2015). "The SeaHorn Verification Framework" - CAV 2015

### Separation Logic Extensions (#25, #26)
28. Raad, A. et al. (2020). "Local Reasoning About the Presence of Bugs" - CAV 2020
29. Kapus, T. & Cadar, C. (2017). "Automatic Testing of Symbolic Executors" - ASE 2017

### Language Extensions (#27, #28)
30. Elliott, A. et al. (2018). "Checked C: Making C Safe by Extension" - SecDev 2018
31. Weiss, A. et al. (2019). "Oxide: The Essence of Rust"
32. Rust RFC 2094: Non-Lexical Lifetimes

### SMT Solvers (#29, #30)
33. Cimatti, A. et al. (2010). "Satisfiability Modulo Theories"
34. de Moura, L. & Bjørner, N. (2008). "Z3: An Efficient SMT Solver" - TACAS 2008
35. de Moura, L. & Bjørner, N. (2007). "Efficient E-Matching for SMT Solvers" - CADE 2007
36. Nelson, G. & Oppen, D. (1979). "Simplification by Cooperating Decision Procedures"

### Industrial Analyzers (#31, #32, #33)
37. Brat, G. et al. (2014). "IKOS: A Framework for Static Analysis" - NFM 2014
38. Kirchner, F. et al. (2015). "Frama-C: A Software Analysis Perspective" - SEFM 2015
39. Avgustinov, P. et al. (2016). "QL: Object-oriented Queries on Relational Data" - ECOOP 2016

### k-Induction (#34)
40. Sheeran, M. et al. (2000). "Checking Safety Properties Using Induction and a SAT-Solver"
41. Donaldson, A. et al. (2011). "Software Verification Using k-Induction" - SAS 2011

### Recurrence Solving (#35)
42. Farzan, A. & Kincaid, Z. (2015). "Compositional Recurrence Analysis"
43. Kincaid, Z. et al. (2017). "Compositional Recurrence Analysis Revisited" - FMCAD 2017

### Loop Acceleration (#36)
44. Kroening, D. et al. (2008). "Under-Approximating Loops in C Programs" - TACAS 2008
45. Jeannet, B. et al. (2014). "APRON: A Library of Numerical Abstract Domains"

### Numeric Invariant Templates (#37)
46. Colón, M. et al. (2003). "Linear Invariant Generation Using Non-linear Constraint Solving" - CAV 2003
47. Sankaranarayanan, S. et al. (2004). "Non-Linear Loop Invariant Generation"

### Sparse Analysis (#38)
48. Oh, H. et al. (2012). "Design and Implementation of Sparse Global Analyses" - PLDI 2012
49. Hardekopf, B. & Lin, C. (2011). "Flow-Sensitive Pointer Analysis for Millions of Lines of Code"

### Context Sensitivity (#39)
50. Shivers, O. (1991). "Control-Flow Analysis of Higher-Order Languages" - PhD thesis
51. Smaragdakis, Y. & Bravenboer, M. (2011). "Pick Your Contexts Well" - POPL 2011

### PDR/IC3 (#40)
52. Bradley, A. (2011). "SAT-Based Model Checking without Unrolling" - VMCAI 2011
53. Cimatti, A. & Griggio, A. (2012). "Software Model Checking via IC3"

### Datalog (#41)
54. Whaley, J. & Lam, M. (2004). "Cloning-Based Context-Sensitive Pointer Alias Analysis"
55. Scholz, B. et al. (2016). "On Fast Large-Scale Program Analysis in Datalog" - CC 2016

### Type State (#42)
56. Strom, R. & Yemini, S. (1986). "Typestate: A Programming Language Concept"
57. Fink, S. et al. (2008). "Effective Typestate Verification in the Presence of Aliasing" - TOSEM

### Value Range (#43)
58. Harrison, W. (1977). "Compiler Analysis of the Value Ranges for Variables"
59. Patterson, J. (1995). "Accurate Static Branch Prediction by Value Range Propagation" - PLDI 1995

### Tools & Standards
60. Clang CFG Documentation: https://clang.llvm.org/doxygen/classclang_1_1CFG.html
61. LLVM Coding Standards: https://llvm.org/docs/CodingStandards.html

### Recent Literature (Part 11, 2022-2026)

#### C++ Standards (#44, #45, #46)
62. P2900R6: "Contracts for C++" (WG21 2025)
63. P3081R2: "Core Safety Profiles for C++26" (WG21 2025)
64. P3274R0: "A Framework for Profiles" (Stroustrup 2024)
65. P3651R0: "Profiles are Essential" (Stroustrup 2025)

#### LLM-Integrated Analysis (#47, #48, #49)
66. Li, H. et al. (2024). "LLift: Enhancing Static Analysis for Practical Bug Detection" - OOPSLA 2024
67. Croft, R. et al. (2024). "IRIS: LLM-Assisted Static Analysis for Security Vulnerabilities"
68. "LLM Meets BMC: Neuro-symbolic Loop Invariant Inference" - ASE 2024
69. "Hybrid Framework: Reasoning LLMs + SMT Solvers" (2025)
70. Endres, M. et al. (2024). "Enchanting Specification Synthesis by LLMs" - CAV 2024
71. "AdaTaint: LLM-Driven Adaptive Source-Sink Identification" (2025)

#### Ecosystem (#50, #51)
72. OASIS SARIF 2.1.0 Standard (2024)
73. "Challenges of Implementing Cross Translation Unit Analysis in Clang Static Analyzer" - IEEE
74. Schubert, P. (2024). "Scaling Static Whole-Program Analysis" - ECOOP 2024

#### Solver & Reliability (#52, #53)
75. "Mariposa: Butterfly Effect in SMT-based Verification" - CMU 2024
76. "Understanding and Detecting Annotation-Induced Faults of Static Analyzers" - FSE 2024

#### Related Tools (#54)
77. "Design and Evaluation of Coconut: Typestates for C++" - ScienceDirect 2025
