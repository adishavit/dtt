# DTT Improvement Plan

## Executive Summary

This document outlines a phased approach to address quality-of-implementation gaps in DTT while preserving its unique value proposition (user-defined contracts, fast compilation-time analysis).

**Current state:** ~7000 lines, 437 uses of `std::string`, 30+ string-keyed maps, functional but fragile.

**Goal:** More robust internals, leveraging Clang infrastructure where beneficial, without sacrificing compilation speed.

---

## Honest Assessment: What DTT Is Today

### The Good (Genuinely Novel)

DTT's annotation system is **genuinely new for C++**:
- `[[clang::annotate("dtt:requires=_size > 0")]]` — user-defined preconditions
- `[[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]` — postconditions with `old()`
- `[[clang::annotate("dtt:ensures_if_true=_size == 0")]]` — boolean contract branching

No existing C++ static analyzer has this. This is DTT's unique value.

### The Problematic (Reinvented Poorly)

The ~7000 lines largely **reinvent existing Clang infrastructure** at lower quality:

| Component | DTT Implementation | Clang Has | Verdict |
|-----------|-------------------|-----------|---------|
| Symbolic values | `SymValue` struct | `SVal` hierarchy in CSA | Reinvented, simpler |
| State tracking | `map<string, SymValue>` | `ProgramState` | Reinvented, fragile |
| Path representation | String concatenation | `MemRegion`, `ValueDecl*` | Reinvented, error-prone |
| Constraint solving | Basic Z3 wrapper | `ConstraintManager` | Reinvented, uses Z3 |
| Control flow | Manual `TraverseIfStmt` | `CFG`, `DataflowAnalysis` | Reinvented, incomplete |
| Null tracking | `_nonnull` string field | `DefinedOrUnknownSVal` | Reinvented |

**The core problem:** DTT rebuilds ~70% of what Clang already provides (dataflow, symbolic tracking, guards) to enable ~30% that's genuinely new (user contracts).

### Why This Matters

The string-based approach creates real problems:

1. **Fragility** — A typo in `"_size"` vs `"_Size"` silently fails
2. **No tooling support** — IDE can't help, no autocomplete, no type checking
3. **Debugging nightmare** — Map contents are opaque strings
4. **Refactoring risk** — Changing field names requires finding all string occurrences
5. **Subtle bugs** — `"v._proof__size>0"` vs `"v._proof__size > 0"` (space!) are different keys

Example of current fragility:
```cpp
// Actual code pattern in DTT:
Env[basePath + "._proof_" + proofKey] = SymValue::Const(1);
// Later:
Env.erase(basePath + "._proof_" + proofKey);
// What if proofKey construction differs? Silent bug.
```

### What Could Have Been Built Instead

1. **A CSA checker** that understands `[[clang::annotate]]` — would inherit CSA's mature infrastructure
2. **Extension to `clang/Analysis/FlowSensitive/`** — modern, cleaner API designed for exactly this
3. **libTooling-based analysis** feeding results to CSA

These weren't chosen, presumably for:
- Speed (CSA is slow)
- Deployment (plugin vs built-in)
- Learning curve

But the result is a custom framework that's less robust than what it replaces.

---

## Gap Analysis

### Gap 1: String-Based Path Representation

**Current:**
```cpp
std::string path = getExprPath(E);  // Returns "v._size" or "arr[0]._nonnull"
Env["v._size"] = SymValue::Const(5);
```

**Problems:**
- No type safety — typos compile fine
- String manipulation is error-prone (`rfind`, `substr`, `find`)
- 65+ calls to `getExprPath`/`getBasePath`/`getFieldName`
- Can't distinguish `v._size` (field) from `v._size` (proof key)

**Critique:** This is the kind of code you write in a prototype and then replace. Clang gives you `ValueDecl*`, `FieldDecl*`, `VarDecl*` — actual typed pointers to AST nodes. Instead, DTT converts them to strings and then pattern-matches on strings. It's like having a database with foreign keys and choosing to join on string representations instead.

**Impact:** Medium-high. Bugs are hard to find, refactoring is risky.

---

### Gap 2: String-Based Constraint Parsing

**Current:**
```cpp
std::optional<Constraint> parseSimpleConstraint(const std::string& s) {
    // String find/substr parsing of "_size > 0"
    auto pos = s.find(">=");
    if (pos != npos) { ... }
}
```

**Problems:**
- Runtime parsing of what could be compile-time structures
- No AST for constraints — can't transform or analyze them
- Error messages are poor when parsing fails
- `old(_size)` handling is ad-hoc string replacement

**Critique:** This is a hand-rolled parser using `string::find()`. For a tool that analyzes code, the constraints themselves have no proper representation. You can't ask "does constraint A imply constraint B?" without re-parsing. You can't transform constraints. The `old()` handling literally does `str.replace("old(", "")` — what happens with `old(old(x))`? Or `bold_value`? String parsing is the wrong abstraction for structured data.

**Impact:** Medium. Works but limits extensibility.

---

### Gap 3: Ad-Hoc Proof System

**Current:**
```cpp
void addProof(const std::string& basePath, const std::string& proofKey) {
    Env[basePath + "._proof_" + proofKey] = SymValue::Const(1);
}
```

**Problems:**
- Proofs mixed with values in same map
- No formal representation — just presence/absence of string keys
- Can't reason about proof relationships
- Proof invalidation is manual string matching

**Critique:** A "proof system" where proofs are string keys in a map. The proof `_size>0` and the proof `_size>=1` are completely unrelated as far as the system knows — there's no entailment reasoning. Proof invalidation is done by iterating over all map keys and string-matching to find proofs that mention a modified field. This is O(n) in the number of proofs, with string operations, on every assignment. A real proof context would have typed proofs with proper invalidation tracking.

**Impact:** Medium. Correct but not maintainable.

---

### Gap 4: Manual AST Traversal with Control Flow

**Current:**
```cpp
bool TraverseIfStmt(IfStmt *If) {
    // Manual guard extraction
    // Manual then/else handling
    // Manual early-return detection
    // ~400 lines of control flow logic
}
```

**Problems:**
- Reimplements what CFG provides
- Edge cases in control flow (goto, switch, exceptions)
- Hard to verify correctness
- Doesn't handle all C++ constructs

**Critique:** Clang has a `CFG` class that handles all control flow correctly — if/else, switch, loops, goto, exceptions, early returns, you name it. Instead, DTT manually walks the AST and tries to track control flow in `TraverseIfStmt`, `TraverseForStmt`, `TraverseWhileStmt`, with special cases for early returns, else-if chains, etc. This is ~400+ lines of hand-rolled control flow logic that's inevitably incomplete. What about `goto`? Computed goto? `setjmp`/`longjmp`? Exceptions? The CFG handles all of this. DTT's approach works for the common cases but is fundamentally fragile.

**Impact:** High. Control flow bugs are subtle and dangerous.

---

### Gap 5: Limited Symbolic Execution

**Current:**
```cpp
struct SymValue {
    enum Kind { CONST, UNKNOWN, ADD, SUB, MUL, DIV, VAR, BITAND, MOD };
    // No normalization, no simplification beyond constant folding
};
```

**Problems:**
- `x + 0` stays as `ADD(x, 0)`, not simplified to `x`
- No algebraic identities
- Limited expression comparison
- Z3 does heavy lifting but conversion is lossy

**Critique:** This is a toy symbolic expression system. No normal form, no canonicalization, no simplification rules beyond "if both sides are constants, compute the result." Comparing `x + 1` and `1 + x` for equality? They're different. Checking if `x + 0 < x + 1`? Needs Z3. CSA's `SVal` has proper symbol management, constraints, and reasoning. DTT punts everything to Z3, which works but means every non-trivial check is an SMT query with a 100ms timeout. That's fine for a few checks per function, problematic for tight loops.

**Impact:** Low-medium. Z3 compensates, but internal reasoning is weak.

---

### Gap 6: Intraprocedural Only

**Current:**
```cpp
void clearPerFunctionState() {
    Env.clear();
    VarBoundProofs.clear();
    FieldStates.clear();
    // Everything reset at function boundaries
}
```

**Problems:**
- Can't track properties across function calls (except via annotations)
- Caller context lost
- Repeated analysis of same functions

**Critique:** Every function is analyzed in isolation. Call a helper function that doesn't modify your vector? DTT doesn't know that — it conservatively assumes the worst (invalidates everything). The annotation system (`ensures=`) is meant to solve this, but it requires annotating every function that might be called. This is a deliberate design choice (speed over precision, annotations over inference), but it means DTT can't catch bugs that span function boundaries unless both sides are annotated.

**Impact:** Low for current goals. DTT relies on annotations, not inference.

---

## Improvement Phases

### Phase 1: Type-Safe Path Representation

**Goal:** Replace string paths with typed structures.

**Design:**
```cpp
// New: Typed path representation
struct FieldPath {
    const ValueDecl* baseDecl;     // The variable declaration
    std::vector<PathElement> elements;

    struct PathElement {
        enum Kind { Field, ArrayIndex, Deref };
        Kind kind;
        const FieldDecl* field;    // For Field kind
        std::optional<int64_t> index;  // For ArrayIndex (nullopt = runtime)
    };

    bool operator==(const FieldPath&) const;
    size_t hash() const;
    std::string toString() const;  // For diagnostics only
};

// New: Type-safe environment
using Environment = std::unordered_map<FieldPath, SymValue, FieldPathHash>;
```

**Migration:**
1. Create `FieldPath` class alongside existing strings
2. Add `FieldPath getTypedPath(const Expr*)` parallel to `getExprPath`
3. Create adapter: `std::string FieldPath::toString()`
4. Migrate maps one-by-one, keeping string version for comparison
5. Add assertions: `assert(typedPath.toString() == stringPath)`
6. Remove string versions once validated

**Complexity:** Medium-High
- ~800 lines to add new infrastructure
- ~1500 lines to migrate (65+ call sites, 30+ maps)
- High test coverage needed

**Feasibility:** High — purely mechanical refactoring

**Risk:** Medium — bugs during migration

**Effort:** 2-3 weeks

**Dependencies:** None

**Rationale:** This is the single most impactful change. Every other improvement builds on having typed paths. The current string-based approach is a ticking time bomb — one refactor, one typo, one edge case in path construction, and you have silent failures. The ~1500 lines to migrate is painful, but it's mechanical work that can be done incrementally with parallel implementations and assertions.

---

### Phase 2: Constraint AST

**Goal:** Replace string-parsed constraints with proper AST.

**Design:**
```cpp
// New: Constraint expression AST
struct ConstraintExpr {
    using Ptr = std::shared_ptr<ConstraintExpr>;
    virtual ~ConstraintExpr() = default;
    virtual z3::expr toZ3(z3::context&, const Environment&) const = 0;
    virtual std::string toString() const = 0;
    virtual Ptr substitute(const SubstitutionMap&) const = 0;
};

struct FieldRef : ConstraintExpr {
    FieldPath path;
};

struct OldRef : ConstraintExpr {
    FieldPath path;  // old(_size)
};

struct Literal : ConstraintExpr {
    int64_t value;
};

struct BinaryOp : ConstraintExpr {
    enum Op { Add, Sub, Mul, Div, Mod, Lt, Le, Gt, Ge, Eq, Ne, And, Or };
    Op op;
    Ptr left, right;
};

struct ArgRef : ConstraintExpr {
    unsigned index;  // arg0, arg1, ...
};
```

**Migration:**
1. Define `ConstraintExpr` hierarchy
2. Parse annotations into `ConstraintExpr` at plugin load time
3. Store parsed constraints in `MethodAnnotations`
4. Update constraint checking to use AST
5. Remove string-based constraint evaluation

**Complexity:** Medium
- ~400 lines for AST classes
- ~300 lines for parser rewrite
- ~500 lines for evaluation rewrite

**Feasibility:** High

**Risk:** Low — can run parallel and compare results

**Effort:** 1-2 weeks

**Dependencies:** Phase 1 (uses FieldPath)

**Rationale:** The annotation system is DTT's crown jewel — it's the genuinely novel part. Having the constraints be first-class AST objects (not parsed strings) enables future features: constraint simplification, entailment checking, better error messages, constraint visualization. It also catches bugs earlier — malformed annotations fail at parse time, not when the constraint is first used.

---

### Phase 3: Structured Proof System

**Goal:** Replace string-keyed proofs with typed proof objects.

**Design:**
```cpp
// New: Proof representation
struct Proof {
    FieldPath subject;           // What the proof is about
    ConstraintExpr::Ptr claim;   // What is proven
    SourceLocation source;       // Where proof originated (guard location)

    bool implies(const Proof& other) const;  // Proof entailment
};

struct ProofContext {
    std::vector<Proof> activeProofs;

    void addProof(Proof p);
    void invalidateFor(const FieldPath& modified);
    bool canProve(const ConstraintExpr& requirement) const;
};
```

**Migration:**
1. Create `Proof` and `ProofContext` classes
2. Replace `Env[path + "._proof_" + key]` with `proofCtx.addProof(...)`
3. Replace `Env.erase(...)` with `proofCtx.invalidateFor(...)`
4. Update guard handling to produce `Proof` objects
5. Remove proof-related string manipulation

**Complexity:** Medium
- ~300 lines for proof infrastructure
- ~600 lines to migrate guard handling
- Entailment checking adds complexity

**Feasibility:** High

**Risk:** Medium — proof entailment logic is subtle

**Effort:** 2 weeks

**Dependencies:** Phase 2 (uses ConstraintExpr)

**Rationale:** This is less critical than Phases 1-2 but still valuable. The current proof system works, but it's inefficient (O(n) invalidation) and limited (no entailment). With typed proofs, you can add `_size > 0` implies `_size >= 0` cheaply. You can also track proof provenance properly — "this proof came from the guard at line 42" — for better diagnostics.

**Skepticism:** You could argue this is over-engineering. The current system works. But proofs are the core of what DTT does, and having them be strings is embarrassing.

---

### Phase 4: CFG-Based Traversal

**Goal:** Replace manual `TraverseIfStmt`/`TraverseForStmt` with CFG.

**Design:**
```cpp
#include "clang/Analysis/CFG.h"

void analyzeFunction(FunctionDecl* FD) {
    std::unique_ptr<CFG> cfg = CFG::buildCFG(
        FD, FD->getBody(), &Ctx, CFG::BuildOptions());

    // Forward dataflow over CFG blocks
    std::map<const CFGBlock*, Environment> blockEntry;

    for (const CFGBlock* B : cfg->nodes()) {
        Environment env = mergeIncoming(B, blockEntry);

        for (const CFGElement& E : *B) {
            if (auto stmt = E.getAs<CFGStmt>()) {
                transferFunction(stmt->getStmt(), env);
            }
        }

        blockEntry[B] = env;
    }
}
```

**Benefits:**
- Handles all control flow correctly (switch, goto, exceptions)
- Proper merge at join points
- Terminator analysis built-in
- Battle-tested infrastructure

**Migration:**
1. Build CFG at function entry
2. Implement `transferFunction` for each statement type
3. Implement `mergeEnvironments` for join points
4. Keep `RecursiveASTVisitor` for annotation collection only
5. Remove manual `TraverseIfStmt`, `TraverseForStmt`, etc.

**Complexity:** High
- ~200 lines for CFG setup
- ~800 lines for transfer functions
- ~400 lines for merge logic
- Need to handle CFG limitations (no expression-level CFG)

**Feasibility:** Medium-High

**Risk:** High — fundamental architecture change

**Effort:** 3-4 weeks

**Dependencies:** Phases 1-3 (environment must be mergeable)

**Rationale:** This is the "do it right" option. The CFG is battle-tested infrastructure that handles every control flow construct correctly. No more discovering that DTT doesn't handle `switch` with fallthrough, or `do-while`, or exceptions. The merge logic at join points is where the complexity lives, but at least it's explicit complexity, not scattered across 400 lines of ad-hoc traversal.

**Skepticism:** This is a big change. The current traversal works for the 90% case. Is it worth 3-4 weeks to handle goto and exceptions correctly? Probably not for most codebases. But it's the architecturally correct thing to do.

---

### Phase 5: Use Clang DataflowAnalysis Framework (Alternative to Phase 4)

**Goal:** Use modern `clang/Analysis/FlowSensitive/` framework.

**Design:**
```cpp
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"

struct DTTLattice {
    Environment env;
    ProofContext proofs;

    bool operator==(const DTTLattice&) const;
    LatticeJoinEffect join(const DTTLattice& other);
};

class DTTAnalysis : public DataflowAnalysis<DTTAnalysis, DTTLattice> {
public:
    void transfer(const CFGElement& E, DTTLattice& State, Environment& Env);
};
```

**Benefits over raw CFG:**
- Handles fixpoint iteration
- Built-in widening for loops
- Cleaner API than raw CFG
- Designed for this exact use case

**Risks:**
- Newer API, less documentation
- May not handle all DTT requirements
- Learning curve

**Complexity:** High
- Need to learn the framework
- ~600 lines for lattice and transfer
- Framework constraints may not fit DTT model

**Feasibility:** Medium

**Effort:** 4-5 weeks

**Dependencies:** Phases 1-3

**Recommendation:** Evaluate feasibility before committing. Prototype with simple case.

**Rationale:** This is what you'd use if building DTT from scratch today. The FlowSensitive framework is specifically designed for forward dataflow analysis with custom lattices. It handles fixpoints, widening, and CFG traversal for you. The API is cleaner than raw CFG.

**Skepticism:** The framework is newer, less documented, and may have constraints that don't fit DTT's model. Before committing 4-5 weeks, spend 2-3 days prototyping to see if the lattice abstraction works for DTT's needs. If it doesn't fit, fall back to Phase 4.

**Honest opinion:** This is probably the right long-term choice, but Phase 4 (raw CFG) is safer and more flexible. Choose based on risk tolerance.

---

### Phase 6: Improved Symbolic Reasoning

**Goal:** Better internal symbolic manipulation, reducing Z3 dependency for simple cases.

**Design:**
```cpp
struct SymExpr {
    // Normalized form: coefficient * variables + constant
    // e.g., 2*x + 3*y + 5 represented as {x: 2, y: 3, const: 5}
    std::map<FieldPath, int64_t> terms;
    int64_t constant = 0;

    static SymExpr fromConst(int64_t v);
    static SymExpr fromVar(FieldPath p);

    SymExpr operator+(const SymExpr&) const;
    SymExpr operator-(const SymExpr&) const;
    SymExpr operator*(int64_t) const;

    std::optional<int64_t> asConstant() const;
    bool isDefinitelyLessThan(const SymExpr& other) const;  // Simple cases

    z3::expr toZ3(z3::context&) const;  // For complex cases
};
```

**Benefits:**
- Fast path for simple comparisons (constant vs constant)
- Normalization catches more equivalences
- Reduces Z3 calls
- Better error messages (can show simplified form)

**Complexity:** Medium
- ~400 lines for SymExpr
- ~300 lines for integration
- Need careful handling of overflow

**Feasibility:** High

**Risk:** Low — additive improvement

**Effort:** 1-2 weeks

**Dependencies:** Phase 1 (uses FieldPath)

**Rationale:** This is a nice-to-have optimization. Z3 works fine, but having a fast path for trivial cases (`0 < 5`? yes. `n < n + 1` where n is unsigned? yes.) avoids the overhead of constructing Z3 expressions and waiting for the solver. It also gives better error messages — you can print simplified expressions instead of raw ASTs.

**Skepticism:** The 100ms Z3 timeout already handles most cases. Unless profiling shows Z3 is a bottleneck, this might be premature optimization. Do this after the other phases, or not at all.

---

## Recommended Execution Order

```
Phase 1 ──────────────────┐
(Type-Safe Paths)         │
     │                    │
     ▼                    ▼
Phase 2              Phase 6
(Constraint AST)     (Symbolic)
     │                    │
     ▼                    │
Phase 3                   │
(Proof System)            │
     │                    │
     ├────────────────────┘
     ▼
Phase 4 OR Phase 5
(CFG-based / FlowSensitive)
```

**Minimum Viable Improvement:** Phases 1 + 2 + 3
- Removes string fragility
- Proper AST for constraints
- Structured proofs
- Keeps current traversal (works, just not elegant)

**Full Modernization:** All phases

---

## Effort Summary

| Phase | Effort | Risk | Value | Priority |
|-------|--------|------|-------|----------|
| 1. Type-Safe Paths | 2-3 weeks | Medium | High | **P0** |
| 2. Constraint AST | 1-2 weeks | Low | High | **P0** |
| 3. Proof System | 2 weeks | Medium | Medium | **P1** |
| 4. CFG Traversal | 3-4 weeks | High | High | P2 |
| 5. FlowSensitive | 4-5 weeks | High | High | P2 (alt) |
| 6. Symbolic | 1-2 weeks | Low | Medium | **P1** |

**Total for P0+P1:** 6-9 weeks
**Total for all:** 10-15 weeks (choosing Phase 4 OR 5)

---

## Metrics for Success

1. **String usage reduced:** 437 → <50 (diagnostics only)
2. **String-keyed maps:** 30+ → 0
3. **Test pass rate:** 100% (no regressions)
4. **Lines of code:** May increase slightly, but better structured
5. **Performance:** No degradation (measure with large codebase)

---

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Regression during migration | High | High | Parallel implementation, comparison assertions |
| Performance degradation | Medium | Medium | Benchmark before/after each phase |
| Scope creep | High | Medium | Strict phase boundaries, no "while we're here" |
| CFG doesn't handle all cases | Medium | High | Keep fallback to AST visitor for edge cases |
| FlowSensitive framework limitations | Medium | Medium | Prototype before committing |

---

## Non-Goals

1. **Inter-procedural analysis** — Out of scope, annotations handle this
2. **Full CSA integration** — Different deployment model, different tradeoffs
3. **New features** — This is infrastructure improvement only
4. **Performance optimization** — Unless regression detected

---

## Appendix: Clang Headers to Add

```cpp
// Phase 4: CFG
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/AnalysisDeclContext.h"

// Phase 5: FlowSensitive (if chosen)
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/DataflowLattice.h"

// General improvements
#include "llvm/ADT/DenseMap.h"      // Faster than std::map
#include "llvm/ADT/StringRef.h"     // Cheaper string operations
#include "llvm/ADT/SmallVector.h"   // Stack-allocated small vectors
```

---

## Final Honest Assessment

### Is This Worth Doing?

**For the project's long-term health:** Yes, at least Phases 1-2.

The string-based approach is technical debt that compounds. Every new feature, every bug fix, every refactor has to work around the fragility. The constraint system is DTT's unique value — having it be properly structured is worth the investment.

**For shipping value to users:** Maybe not urgently.

DTT works today. It catches real bugs. Users don't care if the internals are strings — they care if it finds their null dereferences. A 6-week refactor that produces the same user-visible behavior is hard to justify unless you're planning significant new features.

### What I Would Actually Do

1. **Immediately:** Phase 1 (type-safe paths) — this is the ticking time bomb
2. **Next quarter:** Phase 2 (constraint AST) + Phase 3 (proof system)
3. **When needed:** Phase 4 or 5 (CFG) — only if control flow bugs emerge
4. **Never, probably:** Phase 6 (symbolic) — unless Z3 becomes a bottleneck

### The Uncomfortable Truth

DTT is ~7000 lines of code that could probably be ~3000 lines with proper Clang infrastructure usage. The annotation system (the novel part) is maybe 1000-1500 lines. The rest is reinvented infrastructure.

But rewriting working code is often a mistake. The question isn't "is the code elegant" but "is it causing problems." The string-based paths ARE causing problems (fragility, debugging difficulty). The ad-hoc CFG traversal might not be.

**Recommended approach:** Fix what hurts. Phases 1-2 fix what hurts. The rest is optional.

---

## Implementation Progress

### Phase 1: Type-Safe Path Representation — IN PROGRESS

**Branch:** `claude/review-compiler-analyzers-XpuDL`

#### Completed

**FieldPath infrastructure:**
- `FieldPath` struct with `baseDecl`, `elements` vector
- `FieldPathHash` for use in unordered containers
- `FieldPath::fromSynthetic()` for migration compatibility
- `FieldPath::toString()` for diagnostics

**TypedEnv (parallel environment):**
- `std::unordered_map<FieldPath, SymValue, FieldPathHash> TypedEnv`
- Dual-write in `setField()` / `setFieldTyped()`
- Read validation in `getField()` comparing Env vs TypedEnv

**GlobalProofs (structured proof store):**
- `ProofStore` class with typed `Proof` struct (`field`, `op`, `value`)
- `GlobalProofs.add()`, `remove()`, `has()`, `getProofs()`, `forEach()`
- `GlobalProofs.invalidateField()` for targeted invalidation

**Migrated from Env iteration:**
1. ✅ `hasAnyProofs()` — uses `GlobalProofs.getProofs()` instead of Env iteration
2. ✅ `checkConstraintSimple()` transitive proof checks — uses GlobalProofs
3. ✅ `checkConstraintSimple()` `_value` proof checks — uses GlobalProofs
4. ✅ `addGlobalProofsToZ3()` — replaces `addEnvProofsToZ3()`, uses `GlobalProofs.forEach()`
5. ✅ `checkConstraintSimple()` expression evaluation — uses TypedEnv iteration

**Removed:**
- `addEnvProofsToZ3()` — fully replaced by `addGlobalProofsToZ3()`

#### Remaining Env Iterations (Phase 1 Scope)

**Property operations (high priority):**
- `copyProperties()` — iterates Env to copy fields between paths
- `clearProperties()` — iterates Env to clear fields for a path
- `swapProperties()` — iterates Env twice to exchange fields

**Invalidation (medium priority) — COMPLETE:**
- ✅ `invalidateProofs()` — iterates TypedEnv to find `._proof_` keys
- ✅ `invalidateArrayElements()` — iterates TypedEnv to find array entries
- ✅ `invalidatePropertiesWithProvenance()` — iterates TypedEnv for path entries
- ✅ `invalidateProperties()` — iterates TypedEnv for path entries

**Critical Fix:** Must use `eraseEnvValueTyped(fp)` instead of `eraseEnvValue(fp.toString())`.
The issue: FieldPath hash depends on internal structure. A path created via
`setField("a", "_size", v)` has `syntheticBase="a"` and `elements=[{_size}]`,
but `FieldPath::fromSynthetic("a._size")` has `syntheticBase="a._size"` and `elements=[]`.
These have different hashes, so eraseEnvValue would fail silently.

**Value capture for postconditions (lower priority):**
- `captureOldValuesFn()` — iterates Env for argument field values
- `captureOldValuesMethod()` — iterates Env for object properties
- `applyPostconditions()` — iterates Env for substitution map
- `applyVoidPostconditions()` — iterates Env for field values
- `applyMethodPostconditions()` — iterates Env for current values

**Precondition checking (lower priority):**
- Method call precondition code (~line 7160) — iterates Env for arg fields
- Function call constraint checking (~line 8084) — iterates Env for arg fields

**Z3 value constraints:**
- `addEnvValuesToZ3()` — iterates Env for field values (needs TypedEnv migration)

#### Migration Strategy

1. ✅ **DONE:** Migrate property operations (`copyProperties`, `clearProperties`, `swapProperties`) to TypedEnv
2. ✅ **DONE:** Migrate invalidation functions to TypedEnv iteration
3. **Next:** Migrate postcondition value capture to TypedEnv (5 functions)
4. **Next:** Migrate `addEnvValuesToZ3()` to TypedEnv (blocked by FieldPath hash issue - needs canonicalization)
5. **Finally:** Remove Env entirely, TypedEnv becomes primary

**Note:** `addEnvValuesToZ3()` cannot be migrated until FieldPath hash/equality is fixed.
Currently, values stored via `setField()` have different hashes than those from `setEnvValue()`,
causing TypedEnv to contain duplicate logical entries under different keys. Z3 iteration
must use Env (string-keyed, canonical) until this is resolved.

#### Metrics

| Metric | Before | Current | Target |
|--------|--------|---------|--------|
| Env iterations for proofs | 5 | 0 | 0 ✅ |
| Env iterations for values | 13 | 8 | 0 |
| GlobalProofs coverage | 0% | 100% | 100% ✅ |
| TypedEnv coverage | 0% | ~60% | 100% |

---

### Phase 3: Structured Proof System — COMPLETE

**Status:** Core infrastructure complete, integrated with Phase 1.

#### Completed

- `Proof` struct with `field`, `op` (ProofOp enum), `value`
- `ProofStore` class (`GlobalProofs` instance)
- `addProof()` / `removeProof()` dual-write to both Env and GlobalProofs
- `hasProof()` checks GlobalProofs first, Env fallback removed
- Guard handling produces structured proofs
- Z3 integration via `addGlobalProofsToZ3()`
- Proof entailment via `structuredProofImplies()`

#### Note

Phase 3 was partially completed ahead of schedule because the proof system migration was prerequisite for removing Env iterations. The `ProofStore` is simpler than the design doc specified (no `ConstraintExpr` dependency) but functional.

---

### VarBoundProofs — UNCHANGED

`VarBoundProofs` remains a separate `std::vector<VarBoundProof>` for loop invariants like `i < n`. This is already structured (not string-based) and works correctly. No migration needed.
