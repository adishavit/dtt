# Refactor Plan: Splitting DTTPlugin.cpp (LLM-Optimized)

## Problem

`DTTPlugin.cpp` is ~16,000 lines. A fresh Claude session cannot hold the full file
in context alongside a conversation. Finding the right section for any edit requires
multiple search rounds, and modifications risk unintended interactions with distant code.

The other source files (`DTTAnnotationFile.cpp` ~1,153 lines, `DTTTypes.h` ~866 lines)
are reasonable. This refactor targets `DTTPlugin.cpp` only.

## Design Principle: Optimize for LLM Editing

Traditional refactors optimize for human abstractions (clean interfaces, information
hiding, SOLID). This refactor optimizes for **how Claude actually works with code**:

1. **File as context unit.** Claude reads entire files with the Read tool. The ideal
   file is 1,500-4,000 lines — large enough to be self-contained, small enough to
   fit in working context alongside conversation and other files.

2. **Minimize files-per-task.** A typical edit (fix a bug, add a feature) should
   require reading **at most 2 files**: one types/header file for orientation, plus
   one implementation file containing the code to change.

3. **Grep-ability over encapsulation.** Claude finds things by searching. Names
   like `TypedEnv` are more searchable than `ctx.typedEnv`. Don't wrap globals in
   a context struct for its own sake. The exception is the flow-sensitive state
   (`ProgramState State`): branches copy it and paths join it, so its stores are
   members, under their old names (`State.TypedEnv`) so a search by name still
   finds every access.

4. **Fewer files, not more files.** 14 files (the previous plan) means Claude needs
   multiple rounds of Glob/Grep to locate code. 7 files means one round usually
   suffices. The right number is the minimum that gets every file under ~4,000 lines.

5. **Task-routing via CLAUDE.md.** The most important deliverable is updating the
   CLAUDE.md file-to-task routing table so Claude knows which file to open.

### Anti-patterns (things the previous plan got wrong)

| Anti-pattern | Why it hurts Claude | Fix |
|---|---|---|
| DTTContext struct wrapping globals | Every access becomes `ctx.TypedEnv` instead of `TypedEnv`, adding noise to every line Claude reads. Grep for "TypedEnv" now hits the struct definition AND all access sites. | Keep globals as `extern` in shared header |
| 6 visitor sub-files | A method call chain like `VisitCallExpr → checkCallSitePreconditions → checkConstraintExpr` spans 3 files. Claude reads 3 files to follow one code path. | Split visitor into exactly 2 files at a natural boundary |
| Separate headers per component | `DTTFieldPath.h`, `DTTProofSystem.h`, `DTTZ3Solver.h`, `DTTContext.h`, `DTTVisitor.h` = 5 headers to potentially include. Fresh Claude doesn't know which to read first. | Single `DTTCore.h` — one header to rule them all |
| 400-line files (DTTFieldPath.h, DTTVisitorMemory.cpp) | Below 1,500 lines, the file is too small to justify its existence. Navigation cost of finding the right file exceeds the benefit of smaller files. | Merge small components into larger cohesive files |

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

## Proposed Split: 7 Files

### File 1: `src/DTTCore.h` (~1,500 lines) — "The Map"

**Purpose**: Everything a fresh Claude reads first to orient itself. All types, all
globals, all DenseMapInfo specializations. No implementation logic.

**Contains** (extracted from lines 33-2900 of current file):
- Enums: DTTMode, TrackingState, PointerLifetime, AllocSource, PostconditionTarget
- ModeDependentDiag helper struct
- PathElement, FieldPath, FieldPathHash + `DenseMapInfo<FieldPath>`
- Proof, ProofHash, ProofStore (class definition only) + `DenseMapInfo<Proof>`
- ProofContext (class definition only)
- Comparison pattern structs (SimpleFieldComparison, ArgVsFieldComparison, etc.)
- FieldState, LossProvenance, NullProvenance, StateProvenance
- WarningKey + `DenseMapInfo<WarningKey>`
- Internal Postcondition, BoolPostcondition structs
- HeapObjectState, ScopeExitRequirement, ScopeExitConstraint
- PendingGuardedPostcondition, CallSiteInfo, GuardedReturn, FunctionAnalysisData
- Internal MethodKey, MethodInfo + `DenseMapInfo<MethodKey>`
- FreeFunctionKey/Info (or use DTTTypes.h versions — unify duplicates here)
- VarBoundKey + `DenseMapInfo<VarBoundKey>`
- BaseOriginInfo, PostcondParseResult
- Z3SolverContext, ArgDecomposition
- **All `extern` global variable declarations** (TypedEnv, GlobalProofs,
  GlobalProofContext, ArrayElementFacts, FieldStates, FnPostconditions,
  FnBoolPostconditions, FnScopeExitRequires, TotalFunctions, PointerAliases,
  VarToResource, HeapObjectStates, StackVarPaths, CurrentScopeExitConstraints,
  PendingGuardedPostconditions, FunctionAnalysis, FnPreconditions,
  InferredFnPostconditions, MethodAnnotations, VarBoundProofs,
  PointerBaseOrigins, WarnedLostTracking, ProvenanceChains, AnyContainedTypes,
  GlobalMode, ExternalAnnotationFiles, GlobalCtx)
- Small inline helpers: countOtherHandles, getFunctionKey, hasInternalLinkage,
  getParamIndex, checkProofOp, deferPostcondition

**Namespace**: `namespace dtt::detail { }` for internal types. Globals in `dtt::detail`
namespace with `extern` linkage.

**Why one header**: Claude reads ONE file to understand the entire state model. This
is the "decoder ring" for the codebase. Every grep hit in other files can be resolved
by reading DTTCore.h. No hunting across 5 different headers.

**Includes**: Clang AST headers (for SourceLocation, ASTContext used in struct fields),
LLVM ADT headers, DTTTypes.h, `<z3++.h>`.

### File 2: `src/DTTInfra.cpp` (~3,500 lines) — "The Reasoning Engine"

**Purpose**: All proof, constraint, Z3, and environment manipulation logic. When Claude
needs to fix how a constraint is checked or a proof is managed, everything is here.

**Contains** (extracted from lines 452-5007 of current file):
- **Global variable definitions** (all the `extern` decls from DTTCore.h get their
  storage here — about 50 lines at the top of the file)
- ProofStore/ProofContext method implementations
- `entails()` static function
- Path resolution: getTypedPath, getNestedFieldPathFromThis, hasTrackedFieldInPath
- Symbolic value helpers: overflow-safe arithmetic, buildSymValueFromExpr
- Constraint analysis helpers (all the pattern extraction functions)
- Value concepts registry + expandConcepts + initializeConceptRegistry
- ConstraintExprParser (complete recursive descent parser)
- Nullptr inference, safeEvaluateAsInt, isPlacementNew
- Constraint parsing: translateParamNames, parseConstraintExpr, parsePostcondition
- **FieldPath-native API**: setFieldTyped, getFieldTyped, setEnvValueTyped,
  getEnvValueTyped, mergeTypedEnvBranches, addProof, removeProof, hasProof,
  invalidateProofs, invalidateProperties, copyProperties, clearProperties,
  swapProperties, extractTemplateParams, etc.
- Expression-to-SymValue: exprToSymValueImpl, postcondition inference from impls
- **Z3 integration**: symValueToZ3, constraintExprToZ3, bound string parser,
  addEnvValuesToZ3, addGlobalProofsToZ3, Z3 query functions
- **Constraint checking**: proofImpliesSfc, checkSimpleFieldComparisonWithProofs,
  evaluateConstraintExpr, checkConstraintExprSimple, checkConstraintExpr
- loadSyntheticAnnotations, hasDTTAnnotations, hasPostconditionForArg
- clearPerFunctionState, recordParamFieldWrite
- Inferred pre/postcondition management: beginFunction*, addInferred*, endFunction*

**Why combined**: A constraint check calls into proofs, which calls into Z3, which reads
TypedEnv. These are one call chain. Splitting them into DTTProofSystem.cpp + DTTZ3Solver.cpp
+ DTTUtilities.cpp means Claude follows one logical path across 3 files. Keeping them
together means Claude reads one file and sees the complete reasoning pipeline.

### File 3: `src/DTTAnalysis.cpp` (~1,500 lines) — "Pass 1 + Inference"

**Purpose**: The lightweight first pass and all between-pass computation. Rarely needs
modification unless adding a new inference capability.

**Contains** (extracted from lines 5007-6364 of current file):
- AnalysisCollector class (Pass 1 lightweight RecursiveASTVisitor)
- computeFunctionGhostStates
- propagateParamDereferenced (transitive closure for null deref)
- Expression decomposition: decomposePointerArg, exprToParamSymValue
- Datalog-style fixpoint engine: propagatePreconditionsTransitively
- propagateFrameEffectsTransitively
- emitCallSiteWarnings
- emitTransitivePreconditionWarnings

**Why separate**: This is a distinct compilation phase with clear inputs (AST) and
outputs (FunctionAnalysis map). It uses the globals from DTTCore.h but has no coupling
to the main visitor. A fresh Claude working on transitive propagation reads DTTCore.h
+ DTTAnalysis.cpp and has everything needed.

### File 4: `src/DTTVisitor.h` (~300 lines) — "Visitor Class Declaration"

**Purpose**: The DTTVisitor class definition — member variables, diagnostic IDs, and
all method declarations. No implementations.

**Contains**:
- DTTVisitor class inheriting RecursiveASTVisitor<DTTVisitor>
- All ~80 diagnostic ID member variables
- All private helper method declarations
- All Visit*/Traverse* method declarations
- CurrentFunctionDecl, CurrentScopeDepth, CurrentLoopVar

**Why a separate header**: The visitor class has ~300 lines of member declarations.
Putting this in DTTCore.h would add clutter when Claude just needs types. Putting it
inline in a .cpp would prevent splitting the visitor implementation across two files.
This header exists solely to let two .cpp files share one class definition.

**Includes**: DTTCore.h, Clang RecursiveASTVisitor.

### File 5: `src/DTTVisitorDecls.cpp` (~4,000 lines) — "Declarations & Control Flow"

**Purpose**: The first half of the visitor: everything about declarations, assignments,
and control flow. When Claude needs to fix how a variable is tracked or how an if-branch
is analyzed, this is the file.

**Contains** (from lines 6368-12300 of current DTTVisitor):
- DTTVisitor constructor (diagnostic registration — ~200 lines)
- Utility methods: isInSystemHeader, emitProvenanceChain, emitFixItSuggestion,
  emitIndexFixItSuggestion, emitEstablisherList, emitBackwardSearchForFreeFunc
- emitInferredPrecondition, checkCallSitePreconditions
- TraverseDecl, TraverseLambdaExpr, checkDeallocAnnotation, checkAnnotationConflicts
- VisitCXXMethodDecl
- TraverseFunctionDecl (annotation injection, postcondition inference)
- TraverseCompoundStmt (scope tracking, uninitialized detection)
- Scope-exit machinery: registerScopeExitConstraints, checkSingleConstraint,
  checkScopeExitConstraints, transferConstraints, checkConstraintsBeforeOverwrite,
  checkGuardField, isGuardSatisfied
- applyPostconditions
- trackBufferPointerInit (~619 lines), trackAllocationSize
- **VisitVarDecl** (~882 lines)
- **VisitBinaryOperator** (~754 lines) + guard extraction helpers
- Guard infrastructure: VarGuardInfo, GuardInfo, extractVarGuard, extractGuard
- doesBlockTerminate
- **TraverseIfStmt** (~518 lines)
- **TraverseWhileStmt**, **TraverseForStmt** (~353 lines)
- VisitSwitchStmt

**Split rationale**: This file contains everything that runs "top-to-bottom" through
a function body — declarations, assignments, and branching. The next file contains
everything triggered by call expressions. These two halves rarely interact directly:
a fix to `TraverseIfStmt` doesn't require reading `VisitCallExpr`, and vice versa.

### File 6: `src/DTTVisitorCalls.cpp` (~4,000 lines) — "All Calls"

**Purpose**: The second half of the visitor: everything triggered by function calls,
operator overloads, and expressions. When Claude needs to add support for a new
library function or fix a false positive on a specific call, this is the file.

**Contains** (from lines 12300-15862 of current DTTVisitor):
- **VisitCXXMemberCallExpr** (~453 lines) — method call preconditions, postconditions,
  std::move handling, container operations
- STL namespace helpers: isInStdNamespace, isStdMove, isStdSort, isStdSwap
- **VisitCXXOperatorCallExpr** (~474 lines) — operator=, operator[], comparisons
- Memory safety checks: checkUseAfterFree, checkUninitializedPointer,
  checkUninitializedVar, markStructMembersUninitialized,
  checkStructMembersInitialized, markMemoryFunctionDstInitialized
- emitNullProvenance, recordAllocProvenance
- **VisitUnaryOperator** (~184 lines) — deref, address-of, increment/decrement
- VisitReturnStmt — return value tracking, scope-exit checks
- VisitGCCAsmStmt
- **VisitArraySubscriptExpr** (~135 lines) — array bounds checking
- **VisitMemberExpr** (~156 lines) — member access null/lifetime checking
- **VisitCallExpr** (~1,439 lines) — THE LARGEST FUNCTION: free function handling
  (STL, libc, resource tracking, buffer ops, memcpy/memset/strcmp, preconditions)
- VisitCXXDeleteExpr — double-free/UAF detection
- emitCategoryC, emitCategoryD — diagnostic emission helpers

### File 7: `src/DTTPlugin.cpp` (~200 lines) — "Orchestration"

**Purpose**: Plugin entry point. The only file that knows about all passes.

**Contains** (from lines 15864-15986 of current file):
- DTTConsumer::HandleTranslationUnit — orchestrates Pass 1, between-pass, Pass 2,
  Pass 2b (template instantiations), post-pass transitive propagation
- DTTPlugin — arg parsing (--dtt-mode=, --dtt-annotations=)
- FrontendPluginRegistry registration

**Includes**: DTTCore.h, DTTVisitor.h, DTTAnalysis.h (for AnalysisCollector and
transitive propagation functions).

## Summary

| File | Lines | Role | When Claude reads it |
|---|---|---|---|
| `DTTCore.h` | ~1,500 | All types, all globals | **Always first** — orientation |
| `DTTInfra.cpp` | ~3,500 | Proofs, constraints, Z3, field API | Constraint/proof bugs |
| `DTTAnalysis.cpp` | ~1,500 | Pass 1, fixpoint, transitive propagation | Inference bugs |
| `DTTVisitor.h` | ~300 | Visitor class declaration | Before editing either visitor .cpp |
| `DTTVisitorDecls.cpp` | ~4,000 | Declarations, assignments, control flow | VarDecl/if/for bugs |
| `DTTVisitorCalls.cpp` | ~4,000 | All call expression handling | Call-site bugs |
| `DTTPlugin.cpp` | ~200 | Consumer, plugin, registration | Pass orchestration |
| **Total** | ~15,000 | | |

Plus existing (unchanged): `DTTTypes.h` (866), `DTTAnnotationFile.h` (45),
`DTTAnnotationFile.cpp` (1,153).

## CLAUDE.md Task-Routing Table

**This is the most important deliverable.** Add to CLAUDE.md:

```markdown
### File-to-Task Routing (for Claude sessions)

Read `DTTCore.h` first for any task. Then read the file for your specific task:

| Task | Read this file | Example |
|---|---|---|
| Fix false positive/negative on a function call | `DTTVisitorCalls.cpp` | "fclose not detected", "memcpy false positive" |
| Fix false positive/negative on variable init | `DTTVisitorDecls.cpp` | "vector size not tracked after construction" |
| Fix guard/branch analysis | `DTTVisitorDecls.cpp` | "null check not detected in if-else" |
| Fix loop analysis | `DTTVisitorDecls.cpp` | "for loop bounds not tracked" |
| Fix operator overload handling | `DTTVisitorCalls.cpp` | "operator= not copying properties" |
| Fix constraint checking / Z3 | `DTTInfra.cpp` | "Z3 timeout", "proof not matching" |
| Fix proof management | `DTTInfra.cpp` | "proof lost after assignment" |
| Fix field tracking / environment | `DTTInfra.cpp` | "merge loses value in if-else branch" |
| Fix transitive propagation | `DTTAnalysis.cpp` | "precondition not propagated through call chain" |
| Fix annotation parsing | `DTTAnnotationFile.cpp` | ".dtt file not loading" |
| Add new .dtt annotation | `annotations/*.dtt` | "add zlib support" |
| Add new tracked property | `DTTCore.h` (type) + `DTTInfra.cpp` (logic) + visitor file (checks) | "add _capacity tracking" |
| Fix plugin args / mode | `DTTPlugin.cpp` | "enforce mode not working" |
```

## Globals Strategy: extern, not DTTContext

The ~30 file-scope globals currently live in an anonymous namespace in DTTPlugin.cpp.
To share them across multiple .cpp files:

1. **Declare** all globals as `extern` in `DTTCore.h` inside `namespace dtt::detail`
2. **Define** all globals in `DTTInfra.cpp` (the one file that's always linked)
3. **Use** them directly by name in all other .cpp files via `using namespace dtt::detail`
   or explicit qualification

This preserves the current coding pattern where functions reference `TypedEnv` directly.
No `ctx.` prefix, no parameter threading, no signature changes. Claude sees the same
variable names it would see in the current single-file version.

### Why not DTTContext?

A DTTContext struct would require changing every function signature from:
```cpp
void setFieldTyped(const FieldPath& basePath, StringRef field, SymValue v) {
    TypedEnv[fullPath] = v;
```
to:
```cpp
void setFieldTyped(DTTContext& ctx, const FieldPath& basePath, StringRef field, SymValue v) {
    ctx.TypedEnv[fullPath] = v;
```

This changes ~200+ function signatures and ~1000+ call sites. It adds visual noise to
every line of implementation code. A fresh Claude reading the code after this refactor
would need to mentally translate `ctx.TypedEnv` back to "the environment" every time.
The information content is identical — we're paying a readability tax for no functional
benefit (the plugin is single-threaded, single-TU, and globals are cleared between TUs).

## Execution Plan

### Phase 1: Headers + Infrastructure (one PR)

**Commit 1**: Create `DTTCore.h`
- Extract all types, structs, enums from DTTPlugin.cpp lines 33-2900
- Declare all globals as `extern`
- Add all DenseMapInfo specializations
- Add small inline helpers
- DTTPlugin.cpp: replace extracted code with `#include "DTTCore.h"`
- Tests must pass

**Commit 2**: Create `DTTInfra.cpp`
- Move global variable definitions to DTTInfra.cpp
- Extract proof system, constraint parser, Z3 solver, field API (lines 452-5007)
- DTTPlugin.cpp: replace extracted code with includes + forward references
- Update CMakeLists.txt to compile DTTInfra.cpp
- Tests must pass

**Commit 3**: Create `DTTAnalysis.cpp`
- Extract Pass 1 + transitive propagation (lines 5007-6364)
- Update CMakeLists.txt
- Tests must pass

### Phase 2: Split the visitor (one PR)

**Commit 4**: Create `DTTVisitor.h`
- Extract class definition from DTTPlugin.cpp
- Move all member variable declarations and method declarations to header
- Tests must pass

**Commit 5**: Create `DTTVisitorDecls.cpp`
- Move constructor + declarations/control-flow methods (lines 6368-12300)
- DTTPlugin.cpp retains only DTTConsumer + DTTPlugin + registration
- Update CMakeLists.txt
- Tests must pass

**Commit 6**: Create `DTTVisitorCalls.cpp`
- Move call expression methods (lines 12300-15862)
- Update CMakeLists.txt
- Tests must pass

**Commit 7**: Update CLAUDE.md
- Add file-to-task routing table
- Update architecture/directory structure docs

### Phase 2 ordering note

Within Phase 2, commits 4-5-6 must be sequential (each depends on the previous).
The visitor class definition must exist in the header before splitting implementations
across .cpp files.

## Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Build breakage from include ordering | DTTCore.h is self-contained; DTTVisitor.h includes DTTCore.h; each .cpp includes what it needs |
| Link errors from missing globals | All extern decls in DTTCore.h, all definitions in DTTInfra.cpp — one-to-one correspondence |
| Merge conflicts with feature work | Phase 1 (headers + infra) is the riskiest; do it during a quiet period. Phase 2 (visitor split) is mechanical |
| Anonymous namespace → named namespace | Internal types move to `dtt::detail`. Functions that only one .cpp uses stay in that file's anonymous namespace |
| Duplicate types (DTTTypes.h vs DTTCore.h) | DTTCore.h includes DTTTypes.h and `using`s its types. Internal duplicates (MethodKey, Postcondition) keep their distinct definitions in DTTCore.h — they serve different purposes |
| Performance regression | Zero: no copies, no indirection. Globals remain globals. Inlining may improve slightly due to smaller TU compilation |

## What This Does NOT Do

- **Does not change any logic.** Pure mechanical extraction.
- **Does not rename anything.** Same function names, same variable names.
- **Does not introduce abstractions.** No DTTContext, no interfaces, no vtables.
- **Does not change the anonymous-namespace-global pattern.** Just promotes it to
  extern linkage in a named namespace so multiple .cpp files can share it.
- **Does not split DTTVisitor into more than 2 implementation files.** Two is the
  minimum to get each under ~4,000 lines. More would increase cross-file navigation.
