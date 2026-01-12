# Function Ghost State: Implementation Plan

## Implementation Status

| Feature | Status |
|---------|--------|
| Two-pass architecture | ✅ Implemented |
| @paramN_nonnull inference | ✅ Implemented |
| @returns_nonnull inference | ✅ Implemented |
| Variable tracking (Gap 1) | ✅ Implemented |
| Return value chaining (Gap 2) | ✅ Implemented |
| Conditional nullability (Gap 3) | ✅ Implemented |
| Array element propagation | ✅ Implemented |
| **Null provenance tracking** | ✅ Implemented |
| - Parameter provenance | ✅ Shows where param is declared |
| - Malloc/calloc/realloc provenance | ✅ Shows allocation call site |

**Test coverage:** 12 new test files (600-615 series)

## Overview

Extend DTT to automatically infer and track `@` ghost state properties on functions,
enabling cross-function nullability analysis without manual annotations.

**Key insight from tinyexr analysis:**
- ~95% of "nullability unknown" findings trace to function **parameters**
- ~5% trace to function **return values**
- Solution requires **bidirectional** analysis:
  - **Forward:** Call sites → Parameter nullability
  - **Backward:** Return statements → Return nullability

## Core Concept

```cpp
// Functions become trackable objects in Env, using @ prefix:
Env["ConvertHeader.@param0_nonnull"] = 1;   // All callers pass non-null
Env["ConvertHeader.@param1_nonnull"] = ?;   // Some callers pass unknown
Env["createData.@returns_nonnull"] = 1;     // All returns are non-null

// On function entry, propagate to parameters:
// ConvertHeader.@param0_nonnull → exr_header._nonnull

// On call result assignment, propagate to variable:
// createData.@returns_nonnull → p._nonnull
```

## Architecture: Two-Pass Bidirectional Analysis

```
┌─────────────────────────────────────────────────────────────────┐
│                         PASS 1: COLLECT                         │
│  (Lightweight - just AST references)                            │
│                                                                  │
│  AnalysisCollector visits all functions:                         │
│    • VisitCallExpr: Record call sites (caller, args)            │
│    • VisitReturnStmt: Record return statements                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    BETWEEN PASSES: COMPUTE                       │
│  (Memoized inference)                                            │
│                                                                  │
│  For each function F:                                            │
│    • @paramN_nonnull: Check ALL callers - do they pass non-null?│
│    • @returns_nonnull: Check ALL returns - are they non-null?   │
│                                                                  │
│  Store results in Env with @ prefix                              │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     PASS 2: ANALYZE                              │
│  (Normal DTT analysis with enriched Env)                         │
│                                                                  │
│  TraverseFunctionDecl:                                           │
│    • Lookup F.@paramN_nonnull, inject into param._nonnull       │
│                                                                  │
│  VisitVarDecl / VisitBinaryOperator (assignment from call):     │
│    • Lookup callee.@returns_nonnull, inject into var._nonnull   │
│                                                                  │
│  Rest of analysis proceeds with enriched knowledge               │
└─────────────────────────────────────────────────────────────────┘
```

## Implementation Phases

### Phase 1: Data Structures

Add to DTTPlugin.cpp near line 892 (ENVIRONMENT section):

```cpp
// ============================================================
// FUNCTION GHOST STATE (@ prefix)
// ============================================================

// Call site information collected in Pass 1
struct CallSiteInfo {
    CallExpr* callExpr;
    FunctionDecl* callerFunc;
    SourceLocation loc;
};

// Per-function analysis data
struct FunctionAnalysisData {
    FunctionDecl* decl = nullptr;

    // Forward analysis: call sites where this function is called
    std::vector<CallSiteInfo> callSites;

    // Backward analysis: return statements in this function
    std::vector<ReturnStmt*> returnStmts;

    // Computed results (populated between passes)
    bool analyzed = false;
    std::map<unsigned, std::optional<bool>> paramNonnull;  // paramIdx -> nonnull?
    std::optional<bool> returnsNonnull;                     // all returns nonnull?
};

// Storage: qualified function name -> analysis data
std::map<std::string, FunctionAnalysisData> FunctionAnalysis;

// Helper to get function key
std::string getFunctionKey(FunctionDecl* F) {
    if (!F) return "";
    return F->getQualifiedNameAsString();
}
```

### Phase 2: Pass 1 Collector

Create a lightweight visitor that collects call sites and return statements:

```cpp
// ============================================================
// PASS 1: Analysis Collector
// ============================================================
class AnalysisCollector : public RecursiveASTVisitor<AnalysisCollector> {
    ASTContext& Context;
    FunctionDecl* CurrentFunc = nullptr;

public:
    // Results stored here
    std::map<std::string, FunctionAnalysisData>& FunctionAnalysis;

    AnalysisCollector(ASTContext& Ctx,
                      std::map<std::string, FunctionAnalysisData>& FA)
        : Context(Ctx), FunctionAnalysis(FA) {}

    bool TraverseFunctionDecl(FunctionDecl* F) {
        if (!F || !F->hasBody()) return true;

        CurrentFunc = F;
        std::string key = getFunctionKey(F);
        FunctionAnalysis[key].decl = F;

        RecursiveASTVisitor::TraverseFunctionDecl(F);

        CurrentFunc = nullptr;
        return true;
    }

    // Collect return statements (for backward/return analysis)
    bool VisitReturnStmt(ReturnStmt* RS) {
        if (!CurrentFunc) return true;

        Expr* retVal = RS->getRetValue();
        if (!retVal) return true;  // void return

        // Only track pointer returns
        if (!retVal->getType()->isPointerType()) return true;

        std::string key = getFunctionKey(CurrentFunc);
        FunctionAnalysis[key].returnStmts.push_back(RS);
        return true;
    }

    // Collect call sites (for forward/parameter analysis)
    bool VisitCallExpr(CallExpr* CE) {
        FunctionDecl* callee = CE->getDirectCallee();
        if (!callee) return true;  // Indirect call

        std::string calleeKey = getFunctionKey(callee);

        CallSiteInfo info;
        info.callExpr = CE;
        info.callerFunc = CurrentFunc;
        info.loc = CE->getBeginLoc();

        FunctionAnalysis[calleeKey].callSites.push_back(info);
        return true;
    }
};
```

### Phase 3: Between-Pass Computation

After Pass 1, compute function ghost states:

```cpp
// ============================================================
// COMPUTE FUNCTION GHOST STATES
// ============================================================
void computeFunctionGhostStates() {
    for (auto& [funcKey, data] : FunctionAnalysis) {
        if (data.analyzed) continue;
        data.analyzed = true;

        FunctionDecl* F = data.decl;
        if (!F) continue;

        // --------------------------------------------------------
        // FORWARD: Compute @paramN_nonnull from call sites
        // --------------------------------------------------------
        unsigned numParams = F->getNumParams();
        for (unsigned p = 0; p < numParams; p++) {
            const ParmVarDecl* param = F->getParamDecl(p);
            if (!param->getType()->isPointerType()) continue;

            if (data.callSites.empty()) {
                // No callers found - can't prove anything
                data.paramNonnull[p] = std::nullopt;
                continue;
            }

            bool allNonnull = true;
            for (const auto& site : data.callSites) {
                if (p >= site.callExpr->getNumArgs()) {
                    allNonnull = false;
                    break;
                }

                Expr* arg = site.callExpr->getArg(p)->IgnoreParenImpCasts();
                auto nonnull = inferNonnullFromExpr(arg);

                if (!nonnull.has_value() || *nonnull == 0) {
                    allNonnull = false;
                    break;
                }
            }

            data.paramNonnull[p] = allNonnull ? std::optional(true) : std::nullopt;
        }

        // --------------------------------------------------------
        // BACKWARD: Compute @returns_nonnull from return statements
        // --------------------------------------------------------
        if (!F->getReturnType()->isPointerType()) {
            data.returnsNonnull = std::nullopt;  // Not a pointer return
            continue;
        }

        if (data.returnStmts.empty()) {
            data.returnsNonnull = std::nullopt;  // No returns found
            continue;
        }

        bool allReturnsNonnull = true;
        for (ReturnStmt* RS : data.returnStmts) {
            Expr* retVal = RS->getRetValue();
            if (!retVal) {
                allReturnsNonnull = false;
                break;
            }

            auto nonnull = inferNonnullFromExpr(retVal->IgnoreParenImpCasts());
            if (!nonnull.has_value() || *nonnull == 0) {
                allReturnsNonnull = false;
                break;
            }
        }

        data.returnsNonnull = allReturnsNonnull ? std::optional(true) : std::nullopt;
    }
}

// --------------------------------------------------------
// INJECT INTO ENV WITH @ PREFIX
// --------------------------------------------------------
void injectFunctionGhostStates() {
    for (const auto& [funcKey, data] : FunctionAnalysis) {
        // Inject @paramN_nonnull
        for (const auto& [paramIdx, nonnull] : data.paramNonnull) {
            if (nonnull.value_or(false)) {
                std::string key = funcKey + ".@param" + std::to_string(paramIdx) + "_nonnull";
                Env[key] = SymValue::Const(1);
            }
        }

        // Inject @returns_nonnull
        if (data.returnsNonnull.value_or(false)) {
            std::string key = funcKey + ".@returns_nonnull";
            Env[key] = SymValue::Const(1);
        }
    }
}
```

### Phase 4: Pass 2 Integration

Modify DTTVisitor to use function ghost states:

```cpp
// ============================================================
// PASS 2: Modified DTTVisitor
// ============================================================

// In TraverseFunctionDecl - inject parameter nullability on function entry
bool TraverseFunctionDecl(FunctionDecl* F) {
    // ... existing setup (clear Env, etc.) ...

    // NEW: Inject @paramN_nonnull into parameter._nonnull
    std::string funcKey = getFunctionKey(F);
    auto it = FunctionAnalysis.find(funcKey);
    if (it != FunctionAnalysis.end()) {
        for (unsigned p = 0; p < F->getNumParams(); p++) {
            const ParmVarDecl* param = F->getParamDecl(p);
            if (!param->getType()->isPointerType()) continue;

            auto paramIt = it->second.paramNonnull.find(p);
            if (paramIt != it->second.paramNonnull.end() &&
                paramIt->second.value_or(false)) {
                // All callers pass non-null for this param
                std::string paramName = param->getNameAsString();
                setField(paramName, "_nonnull", SymValue::Const(1));

                // Also set tracking state to Known
                FieldStates[paramName + "._nonnull"] =
                    FieldState::Known(SymValue::Const(1));
            }
        }
    }

    // Continue with normal traversal
    bool result = RecursiveASTVisitor::TraverseFunctionDecl(F);
    return result;
}

// In VisitVarDecl - inject return nullability when assigning from call
bool VisitVarDecl(VarDecl* VD) {
    // ... existing code ...

    // NEW: Check if init is a call to a function with @returns_nonnull
    if (VD->getType()->isPointerType() && VD->hasInit()) {
        Expr* init = VD->getInit()->IgnoreParenImpCasts();

        if (auto* CE = dyn_cast<CallExpr>(init)) {
            if (FunctionDecl* callee = CE->getDirectCallee()) {
                std::string calleeKey = getFunctionKey(callee);

                // Check Env for callee.@returns_nonnull
                auto envIt = Env.find(calleeKey + ".@returns_nonnull");
                if (envIt != Env.end() && envIt->second.kind == SymValue::CONST
                    && envIt->second.constVal == 1) {
                    std::string varName = VD->getNameAsString();
                    setField(varName, "_nonnull", SymValue::Const(1));
                }
            }
        }
    }

    return true;
}

// Similarly in VisitBinaryOperator for assignments: p = someFunc();
```

### Phase 5: Consumer Orchestration

The DTTConsumer orchestrates both passes:

```cpp
// ============================================================
// DTT CONSUMER - Two-Pass Orchestration
// ============================================================
class DTTConsumer : public ASTConsumer {
    ASTContext* Context;

public:
    void HandleTranslationUnit(ASTContext& Ctx) override {
        Context = &Ctx;

        // ====================================================
        // PASS 1: Collect call sites and return statements
        // ====================================================
        AnalysisCollector collector(Ctx, FunctionAnalysis);
        collector.TraverseDecl(Ctx.getTranslationUnitDecl());

        // ====================================================
        // BETWEEN PASSES: Compute function ghost states
        // ====================================================
        computeFunctionGhostStates();
        injectFunctionGhostStates();

        // ====================================================
        // PASS 2: Main DTT analysis with enriched Env
        // ====================================================
        DTTVisitor visitor(Ctx);
        visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
    }
};
```

### Phase 6: Annotation Override Support

Users can override inferred ghost states with explicit annotations:

```cpp
// ============================================================
// ANNOTATION SUPPORT
// ============================================================

// New annotations:
// [[clang::annotate("dtt:assumes=@param0_nonnull=1")]]  - param is always non-null
// [[clang::annotate("dtt:assumes=@param0_nonnull=0")]]  - param CAN be null
// [[clang::annotate("dtt:ensures=result._nonnull=1")]]  - return is always non-null (existing)

// Process in computeFunctionGhostStates(), BEFORE inference:
void applyAnnotationOverrides(FunctionDecl* F, FunctionAnalysisData& data) {
    for (auto* A : F->attrs()) {
        if (auto* Ann = dyn_cast<AnnotateAttr>(A)) {
            StringRef s = Ann->getAnnotation();

            // Check for @paramN_nonnull override
            if (s.starts_with("dtt:assumes=@param")) {
                // Parse: @param0_nonnull=1 or @param0_nonnull=0
                // Extract param index and value
                // Override data.paramNonnull[idx]
            }

            // Check for result._nonnull (existing annotation)
            if (s.starts_with("dtt:ensures=result._nonnull=")) {
                StringRef val = s.substr(28);
                if (val == "1") {
                    data.returnsNonnull = true;  // Override inference
                } else if (val == "0") {
                    data.returnsNonnull = false;
                }
            }
        }
    }
}
```

**Annotation precedence:** Explicit annotation > Inferred ghost state

## Test Cases

### Test 1: Parameter All-Callers-Nonnull (Pass) - HIGH PRIORITY

```cpp
// 600_function_ghost_param_pass.cpp
// Tests: All callers pass non-null → parameter proven non-null
struct Data { int value; };

void process(Data* d) {
    d->value = 42;  // Should PASS - all callers pass non-null
}

int main() {
    Data local;
    Data* heap = new Data();

    process(&local);   // Caller 1: &local is non-null
    process(heap);     // Caller 2: new is non-null

    return 0;
}
// DTT infers: process.@param0_nonnull = 1
// Inside process(): d._nonnull = 1
// No warning on d->value
```

### Test 2: Parameter Some-Callers-Unknown (Fail) - HIGH PRIORITY

```cpp
// 601_function_ghost_param_fail.cpp
// Tests: Some callers pass unknown → parameter NOT proven
struct Data { int value; };

Data* maybeGet();  // Unknown return

void process(Data* d) {
    d->value = 42;  // Should FAIL - some callers may pass null
}

int main() {
    Data local;
    Data* unknown = maybeGet();

    process(&local);    // Caller 1: non-null
    process(unknown);   // Caller 2: UNKNOWN!

    return 0;
}
// DTT infers: process.@param0_nonnull = nullopt (can't prove)
// Warning remains - this is CORRECT (unknown could be null)
```

### Test 3: Parameter Derivation Chain (Pass)

```cpp
// 602_function_ghost_param_chain_pass.cpp
// Tests: Derived pointers inherit parameter nullability
struct Data { int value; };

void process(Data* d) {
    Data* local = d;      // local derives from d
    Data* other = local;  // other derives from local
    other->value = 42;    // Should PASS if d proven non-null
}

int main() {
    Data obj;
    process(&obj);  // Only caller, passes non-null
    return 0;
}
```

### Test 4: Return Value Inference (Pass)

```cpp
// 603_function_ghost_return_pass.cpp
// Tests: All returns non-null → return proven non-null
struct Data { int value; };

Data* createData() {
    return new Data();  // Only return, always non-null
}

int main() {
    Data* p = createData();
    p->value = 42;  // Should PASS - createData.@returns_nonnull = 1
    return 0;
}
```

### Test 5: Return Value Can-Be-Null (Fail)

```cpp
// 604_function_ghost_return_fail.cpp
// Tests: Some returns null → return NOT proven
struct Data { int value; };

Data* maybeCreate(bool flag) {
    if (flag) return new Data();
    return nullptr;  // One path returns null
}

int main() {
    Data* p = maybeCreate(true);
    p->value = 42;  // Should FAIL - maybeCreate can return null
    return 0;
}
```

### Test 6: Forward Declaration (Pass)

```cpp
// 605_function_ghost_forward_pass.cpp
// Tests: Two-pass handles forward declarations
struct Data { int value; };

void process(Data* d);  // Forward declaration

int main() {
    Data obj;
    process(&obj);  // Call before definition
    return 0;
}

void process(Data* d) {
    d->value = 42;  // Should PASS - caller passes non-null
}
```

### Test 7: Annotation Override

```cpp
// 606_function_ghost_override_pass.cpp
// Tests: Explicit annotation overrides inference
struct Data { int value; };

// User says: trust me, param0 is always non-null
[[clang::annotate("dtt:assumes=@param0_nonnull=1")]]
void process(Data* d) {
    d->value = 42;  // Should PASS - annotation trusted
}

int main() {
    Data* unknown = getMaybeNull();
    process(unknown);  // Caller passes unknown, but function trusts it
    return 0;
}
```

## Implementation Order

### Priority 1: Parameter Tracking (95% of findings)

1. Add data structures (`FunctionAnalysisData`, `CallSiteInfo`)
2. Create `AnalysisCollector` (Pass 1) - collect call sites
3. Add `computeFunctionGhostStates()` - compute `@paramN_nonnull`
4. Modify `TraverseFunctionDecl` - inject param nullability
5. **Test:** 600, 601, 602 (parameter tests)

### Priority 2: Return Tracking (5% of findings)

6. Extend `AnalysisCollector` to collect return statements
7. Extend computation for `@returns_nonnull`
8. Modify `VisitVarDecl` - inject return nullability
9. **Test:** 603, 604, 605 (return tests)

### Priority 3: Polish

10. Add annotation override support (`dtt:assumes=@param...`)
11. **Test:** 606 (override test)
12. Run on tinyexr, measure FP reduction
13. Documentation

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Performance (two passes) | Pass 1 is lightweight (no inference, just collection) |
| Complex control flow | Conservative: any unknown caller = param not proven |
| Templates | Analyze each instantiation separately |
| Recursion (A calls B calls A) | Detect cycles, treat as unknown |
| Virtual methods | Can't determine callee, treat as unknown |
| Indirect calls (`funcPtr()`) | Can't determine callee, skip |

## Success Criteria

1. Tests 600-606 pass
2. tinyexr FP reduction: **~60% fewer** null warnings (from 460 to ~180)
3. No false negatives introduced (no missed real bugs)
4. Compile time impact < 15% increase
5. Remaining warnings are **real risks** (callers passing unknown)

## Future Considerations

### Option C: CallGraph-Based Topological Analysis

Instead of two-pass, use Clang's `CallGraph` to analyze in dependency order:

```cpp
#include "clang/Analysis/CallGraph.h"

void HandleTranslationUnit(ASTContext& Ctx) {
    CallGraph CG;
    CG.addToCallGraph(Ctx.getTranslationUnitDecl());

    // Analyze in reverse topological order (leaves first)
    // Callees always analyzed before callers
    for (FunctionDecl* F : reverseTopoSort(CG)) {
        analyzeFunction(F);
    }
}
```

**Pros:**
- Single pass through each function
- More elegant data flow
- Handles mutual recursion explicitly

**Cons:**
- Imprecise for indirect calls / virtual methods
- More invasive refactor of DTT architecture
- May not handle all edge cases

**Decision:** Start with two-pass (Option A), consider CallGraph (Option C) as optimization if performance is an issue.

### Other Future Extensions

- **Cross-TU analysis:** Export function summaries to JSON, import in other TUs
- **More ghost states:** `@throws`, `@modifies_param0`, `@pure`
- **Clang nullability integration:** Respect `_Nullable`, `_Nonnull` attributes
- **Recursive return analysis:** If `return foo()`, analyze `foo()`'s returns
