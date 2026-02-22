# Refactor Plan: Splitting DTTPlugin.cpp

## Problem

`DTTPlugin.cpp` is ~16,000 lines. It contains the entire plugin in a single file:
enums, data structures, proof system, constraint parser, Z3 integration, two AST
visitors, transitive analysis, and all diagnostic emission. This makes navigation
difficult and compilation slow.

The other source files (`DTTAnnotationFile.cpp` at 1,153 lines, `DTTTypes.h` at
866 lines) are reasonable in size. The refactor targets `DTTPlugin.cpp` only.

## Current Structure (line ranges)

```
DTTPlugin.cpp (15,986 lines)
├── 1-130      Mode config, enums (TrackingState, PointerLifetime)
├── 125-452    FieldPath, PathElement                              (~330 lines)
├── 452-1000   Proof, ProofStore, ProofContext                     (~550 lines)
├── 1000-1120  Symbolic value helpers                              (~120 lines)
├── 1120-1300  Constraint analysis helpers (comparisons)           (~180 lines)
├── 1300-1458  Value concepts registry                             (~160 lines)
├── 1458-1844  ConstraintExprParser                                (~386 lines)
├── 1844-1950  Nullptr inference, safeEvaluateAsInt                (~106 lines)
├── 1950-2060  FieldState, provenance structs                      (~110 lines)
├── 2060-2140  Constraint parsing helpers                          (~80 lines)
├── 2140-2285  Postcondition/BoolPostcondition (internal)          (~145 lines)
├── 2285-2550  Environment, resources, heap, stack, scope-exit,    (~265 lines)
│              function ghost state, FunctionAnalysisData
├── 2547-2810  MethodKey, MethodInfo, VarBoundKey (internal)       (~260 lines)
├── 2810-2900  InferredPreconditions, BaseOriginInfo               (~90 lines)
├── 2900-3035  InferredPostconditions                              (~135 lines)
├── 3035-3985  FieldPath-native utility functions                  (~950 lines)
├── 3985-4228  Forward declarations, PostcondParseResult           (~243 lines)
├── 4228-4784  Z3 solver + helpers                                 (~556 lines)
├── 4784-5007  Constraint checking                                 (~223 lines)
├── 5007-5231  Pass 1: AnalysisCollector                           (~224 lines)
├── 5231-5965  Ghost states, transitive propagation, fixpoint      (~734 lines)
├── 5965-6364  Call-site warning emission, transitive checking     (~399 lines)
├── 6364-15862 DTTVisitor (Pass 2)                                 (~9,500 lines)
│   ├── 6368-6458   Member variables (diagnostics)                 (~90 lines)
│   ├── 6458-7290   Utility methods, precondition checking         (~832 lines)
│   ├── 7290-7475   TraverseDecl, Lambda, annotation conflicts     (~185 lines)
│   ├── 7475-7730   TraverseFunctionDecl, TraverseCompoundStmt     (~255 lines)
│   ├── 7730-8005   Scope-exit constraint checking                 (~275 lines)
│   ├── 8005-8200   Guard/postcondition application                (~195 lines)
│   ├── 8200-8900   Buffer pointer & allocation tracking           (~700 lines)
│   ├── 8901-9784   VisitVarDecl                                   (~883 lines)
│   ├── 9784-11302  VisitBinaryOperator + helpers                  (~1,518 lines)
│   ├── 11302-11840 TraverseIfStmt                                 (~538 lines)
│   ├── 11840-12300 TraverseWhileStmt, TraverseForStmt             (~460 lines)
│   ├── 12300-12810 VisitSwitchStmt, VisitCXXMemberCallExpr        (~510 lines)
│   ├── 12810-13025 Std namespace helpers (isStdMove, isStdSort)   (~215 lines)
│   ├── 13025-13550 VisitCXXOperatorCallExpr                       (~525 lines)
│   ├── 13550-13720 UAF/uninitialized checks                       (~170 lines)
│   ├── 13720-14310 VisitUnaryOperator, VisitReturnStmt, etc.      (~590 lines)
│   └── 14310-15862 VisitCallExpr, VisitCXXDeleteExpr, Cat C/D     (~1,552 lines)
├── 15864-15952 DTTConsumer (HandleTranslationUnit)                (~88 lines)
└── 15954-15987 DTTPlugin (arg parsing, registration)              (~33 lines)
```

## Coupling Analysis

The main coupling challenge: **most code lives in a single anonymous namespace
with ~30 file-scope global variables** (TypedEnv, GlobalProofs, FunctionAnalysis,
MethodAnnotations, etc.). Both AnalysisCollector and DTTVisitor read/write these
globals, and DTTConsumer orchestrates them.

This means we cannot naively split into independent compilation units without
first introducing a shared context struct to replace the globals.

## Proposed Split

### Phase 1: Extract self-contained components (no shared state changes)

These components have clean boundaries and can be extracted without changing the
global state model:

#### 1. `src/DTTFieldPath.h` (~400 lines)
Extract from lines 125-452:
- `PathElement` struct
- `FieldPath` struct
- `FieldPathHash` struct
- `llvm::DenseMapInfo<FieldPath>` specialization

**Why standalone**: Pure data type with no dependencies on plugin state. Already
has a clear section boundary. Used by ProofStore, TypedEnv, and the visitor.

#### 2. `src/DTTProofSystem.h` + `src/DTTProofSystem.cpp` (~600 lines)
Extract from lines 454-1000:
- `Proof` struct, `ProofHash`
- `ProofStore` class
- `ProofContext` class
- `entails()` static function

**Why standalone**: Self-contained proof logic. Depends only on FieldPath and
ProofOp (already in DTTTypes.h). No dependency on global state — ProofStore
instances are passed by reference.

#### 3. `src/DTTConstraintParser.h` + `src/DTTConstraintParser.cpp` (~600 lines)
Extract from lines 1120-1844:
- Constraint analysis helpers (SimpleFieldComparison, ArgVsFieldComparison, etc.)
- Value concepts registry + `expandConcepts()`
- `ConstraintExprParser` class

**Why standalone**: Pure parsing/analysis. Depends on ConstraintExpr (in
DTTTypes.h) and SymValue (in DTTTypes.h). No global state dependency.

#### 4. `src/DTTZ3Solver.h` + `src/DTTZ3Solver.cpp` (~800 lines)
Extract from lines 4228-5007:
- `Z3SolverContext` struct
- `sanitizeForSMT()`
- Bound string parser
- Z3 helpers for TypedEnv and GlobalProofs
- `checkConstraintExprWithZ3()`
- `proofImpliesSfc()`, `checkExprOp()`, and constraint checking functions

**Why standalone**: Z3 integration is well-isolated. Takes ProofStore/TypedEnv
by reference. A few functions need access to TypedEnv and GlobalProofs — these
would take them as parameters instead of reading globals.

### Phase 2: Extract analysis passes (requires shared context)

#### 5. `src/DTTContext.h` (~400 lines)
Introduce a `DTTContext` struct that owns all the current file-scope globals:
- `TypedEnv`, `GlobalProofs`, `ArrayElementFacts`
- `FnPostconditions`, `FnBoolPostconditions`, `FnScopeExitRequires`
- `MethodAnnotations`, `FreeFunctionAnnotations`
- `FunctionAnalysis`, `FnPreconditions`, `InferredFnPostconditions`
- `VarToResource`, `HeapObjectStates`, `StackVarPaths`
- `CurrentScopeExitConstraints`, `PendingGuardedPostconditions`
- `PointerAliases`, `PointerBaseOrigins`, `VarBoundProofs`
- `GlobalMode`, `ExternalAnnotationFiles`
- All `clearPerFunctionState()` and helper methods

Also extract from lines 1950-2060, 2140-2285, 2285-2550, 2547-2900:
- `FieldState`, `LossProvenance`, `NullProvenance`, `StateProvenance`
- Internal `Postcondition`, `BoolPostcondition`
- `HeapObjectState`, `ScopeExitRequirement`, `ScopeExitConstraint`
- `PendingGuardedPostcondition`, `CallSiteInfo`, `GuardedReturn`
- `FunctionAnalysisData`
- `WarningKey`, `BaseOriginInfo`
- Internal `MethodKey`, `MethodInfo` (plugin's own, distinct from DTTTypes.h)

**Why separate**: Centralizes all mutable shared state. Every other component
takes `DTTContext&` instead of reaching for globals. This is the key enabler
for Phase 2 splits.

#### 6. `src/DTTAnalysis.h` + `src/DTTAnalysis.cpp` (~1,400 lines)
Extract from lines 5007-6364:
- `AnalysisCollector` class (Pass 1)
- `computeFunctionGhostStates()`
- `propagateParamDereferenced()`
- Expression decomposition for transitive propagation
- Datalog-style fixpoint engine
- `propagateFrameEffectsTransitively()`
- `emitCallSiteWarnings()`
- `emitTransitivePreconditionWarnings()`

**Why separate**: Complete Pass 1 + inter-pass analysis. Clear input/output
boundary: reads AST, writes to FunctionAnalysis. Takes `DTTContext&`.

#### 7. `src/DTTUtilities.h` + `src/DTTUtilities.cpp` (~1,000 lines)
Extract from lines 3035-4228:
- FieldPath-native API (getField, setField, hasProof, etc.)
- Forward declarations cleanup
- `PostcondParseResult` and postcondition parsing helpers
- `translateParamNames()`, `isGhostField()`, `mergeAbsentField()`

**Why separate**: Utility functions used by both visitors. Most take
ProofStore/TypedEnv as parameters (or will after DTTContext refactor).

### Phase 3: Split DTTVisitor (the big win)

DTTVisitor at ~9,500 lines is the bulk of the problem. After Phase 2 gives
us DTTContext, the visitor's method implementations can be split across
multiple `.cpp` files while the class definition stays in one header.

#### 8. `src/DTTVisitor.h` (~300 lines)
DTTVisitor class definition with all member variables and method declarations.
The constructor stays here (diagnostic ID initialization).

#### 9. `src/DTTVisitorCore.cpp` (~1,500 lines)
- Constructor / diagnostic initialization
- Utility methods (isInSystemHeader, emitProvenanceChain, emitFixItSuggestion, etc.)
- checkCallSitePreconditions
- TraverseDecl, TraverseLambdaExpr
- Scope-exit constraint methods
- Guard/postcondition application methods

#### 10. `src/DTTVisitorDecls.cpp` (~1,800 lines)
- VisitCXXMethodDecl, TraverseCXXMethodDecl, TraverseFunctionDecl
- TraverseCompoundStmt
- VisitVarDecl (including buffer pointer tracking)

#### 11. `src/DTTVisitorExprs.cpp` (~2,000 lines)
- VisitBinaryOperator
- VisitUnaryOperator
- VisitArraySubscriptExpr
- VisitMemberExpr

#### 12. `src/DTTVisitorControlFlow.cpp` (~1,000 lines)
- doesBlockTerminate
- TraverseIfStmt
- TraverseWhileStmt
- TraverseForStmt
- VisitSwitchStmt

#### 13. `src/DTTVisitorCalls.cpp` (~2,800 lines)
- VisitCXXMemberCallExpr
- isStdMove, isStdSort, isInStdNamespace
- VisitCXXOperatorCallExpr
- VisitCallExpr
- VisitCXXDeleteExpr

#### 14. `src/DTTVisitorMemory.cpp` (~400 lines)
- checkUseAfterFree, checkUninitializedPointer, checkUninitializedVar
- markStructMembersUninitialized, checkStructMembersInitialized
- markMemoryFunctionDstInitialized
- emitNullProvenance, recordAllocProvenance

### Resulting `src/DTTPlugin.cpp` (~200 lines)
What remains:
- `DTTConsumer` class (HandleTranslationUnit orchestration)
- `DTTPlugin` class (arg parsing)
- Plugin registration

## Summary

| File | Lines (approx) | Content |
|------|----------------|---------|
| `DTTPlugin.cpp` | 200 | Plugin entry, consumer, registration |
| `DTTFieldPath.h` | 400 | FieldPath type |
| `DTTProofSystem.h/.cpp` | 600 | Proof, ProofStore, ProofContext |
| `DTTConstraintParser.h/.cpp` | 600 | Parsing, analysis helpers, concepts |
| `DTTZ3Solver.h/.cpp` | 800 | Z3 integration |
| `DTTContext.h` | 400 | Shared state struct |
| `DTTAnalysis.h/.cpp` | 1,400 | Pass 1, fixpoint, transitive propagation |
| `DTTUtilities.h/.cpp` | 1,000 | FieldPath-native API, helpers |
| `DTTVisitor.h` | 300 | Visitor class declaration |
| `DTTVisitorCore.cpp` | 1,500 | Visitor utilities, scope-exit, guards |
| `DTTVisitorDecls.cpp` | 1,800 | VarDecl, MethodDecl, FunctionDecl |
| `DTTVisitorExprs.cpp` | 2,000 | Binary/Unary/Array/Member expressions |
| `DTTVisitorControlFlow.cpp` | 1,000 | If/While/For/Switch |
| `DTTVisitorCalls.cpp` | 2,800 | All call expression handling |
| `DTTVisitorMemory.cpp` | 400 | UAF, uninitialized, lifetime |
| **Total** | ~15,200 | (some glue removed, some headers added) |

## Execution Order

1. **Phase 1 first** — no risk, no state changes, just extraction of pure types/logic
2. **Phase 2** — introduce DTTContext, migrate globals, extract analysis passes
3. **Phase 3** — split DTTVisitor across multiple .cpp files

Each phase should be a separate PR. Each file extraction within a phase should
be a separate commit. Full test suite must pass after every commit.

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Build breakage from circular includes | Phase 1 types have no cycles by design |
| Performance regression from indirection | DTTContext is passed by reference, no copies |
| Merge conflicts with feature work | Do Phase 1 first (small, safe); coordinate Phase 2/3 timing |
| Anonymous namespace leakage | New files use `namespace dtt { namespace detail { ... } }` for internal types |
| Duplicate MethodKey/Postcondition types | Plugin's internal versions coexist with DTTTypes.h versions; consider unifying in Phase 2 |
