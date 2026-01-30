# Loop Analysis & Path Sensitivity Implementation Plan

This document details the implementation plan for two advanced analysis features:
- **#3: Widening/Narrowing for Loops** - Abstract interpretation for loop invariants
- **#5: Incorrectness Logic** - Path-sensitive analysis for definite bug detection

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

### Immediate Implementation (Parts 1-3)

| Feature | Complexity | Effort | Priority |
|---------|------------|--------|----------|
| #3 Widening/Narrowing | Medium-High | 4-6 weeks | High |
| #5 Incorrectness Logic | Medium | 6-8 weeks | Medium |
| Shared Infrastructure | Medium | 2-3 weeks | Required |

**Total Estimated Effort**: 10-14 weeks (with parallelization)

### Literature Survey (Part 7)

| Status | Count | Techniques |
|--------|-------|------------|
| ✅ Implemented | 3 | #1 Frame Inference, #4 Effects, #10 Refutations |
| 📋 In Plan | 2 | #3 Widening, #5 Incorrectness |
| 🔴 High Priority | 2 | #11 Demand-Driven, #19 Taint |
| ⚪ Medium Priority | 6 | #7, #8, #12, #13, #16, #17 |
| ⚫ Low Priority | 7 | #2, #6, #9, #14, #15, #18, #20 |

**Total Techniques Cataloged**: 20

---

## Part 1: Widening/Narrowing for Loops

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

**Why High Priority:** Directly addresses bugs where attacker controls size parameter.

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

### Priority Summary

| Priority | Techniques |
|----------|------------|
| 🔴 **High** | #11 Demand-Driven, #19 Taint Analysis |
| 🟡 **Medium-High** | #13 Interpolation/CEGAR |
| 🟢 **Already Partial** | #15 Summaries |
| ⚪ **Medium** | #7 Ownership, #8 Relational, #12 Predicate, #16 Octagons, #17 Merging |
| ⚫ **Low** | #2 Alias, #6 Gradual, #9 2nd-Order, #14 Liquid, #18 BMC, #20 ACDL |

### Recommended Next Steps

1. **#19 Taint Analysis** - Directly catches Heartbleed-style bugs
2. **#11 Demand-Driven** - Improves performance on large codebases
3. **#13 CEGAR** - Automatically discovers loop invariants
4. **#7 Ownership** - Enhances existing `std::move` tracking

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

### Tools & Standards
21. Clang CFG Documentation: https://clang.llvm.org/doxygen/classclang_1_1CFG.html
22. LLVM Coding Standards: https://llvm.org/docs/CodingStandards.html
