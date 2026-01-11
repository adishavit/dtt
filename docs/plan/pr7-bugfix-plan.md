# PR #7 Bugfix Plan

## Phase 1: Understand DTTPlugin.cpp
- [x] Read and grok the analyzer architecture
- [x] Document key data structures (Env, VarBoundProofs, FieldStates, etc.)
- [x] Understand guard extraction and proof propagation

### Key Data Structures
- `Env` - Maps "varPath.field" to SymValue (known values)
- `FieldStates` - Maps "varPath.field" to FieldState (tracking state: Known/Lost/Solver/Untracked)
- `VarBoundProofs` - Vector of proofs like "i < v._size"
- `MethodAnnotations` - DTT annotations for methods

### Key Functions
- `extractVarGuard()` (line ~3028) - Extracts variable guard proofs like `i < v._size`
- `extractGuard()` (line ~3120) - Extracts general field guard proofs
- `TraverseIfStmt()` (line ~3430) - Handles if-statement proof propagation
- Lines ~4583-4614 - Free function constraint checking

## Phase 2: Rename Test (1 test)
- [x] **308** `_silent.cpp` → `_fail.cpp` (DTT correctly catches guard leak)

## Phase 3: Quick Wins - LOW Complexity (4 bugs)

### 313 - arg1 requires checked against arg0 path
- [x] Root cause: Constraint parsing didn't extract argN from "argN._field" format; checking always used arg0
- [x] Fix: Added parsing at lines 675-690, use correct argIdx at lines 4583-4592
- [x] Tests: `313_arg1_requires_pass.cpp` (3 functions), `313b_arg1_requires_fail.cpp`

### 315 - Reversed comparison `v._size > i` not recognized
- [x] Root cause: `extractVarGuard()` only handled DeclRefExpr on LHS, not RHS
- [x] Fix: Added reversed form handling at lines 3010-3092, flips operator when field is on LHS
- [x] Tests: `315_guard_reversed_comparison_pass.cpp` (2 functions), `315b_guard_reversed_fail.cpp`

### 323 - Double negation `!(ptr == nullptr)` not recognized
- [x] Root cause: `extractGuard()` didn't recurse through `!` operator wrapping comparisons
- [x] Fix: Added recursive `!` handling at lines 3120-3134, swaps proofKey/negProofKey
- [x] Tests: `323_double_negation_guard_pass.cpp` (2 functions), `323b_double_negation_fail.cpp`

### 361 - Assignment `int j = i;` loses proof
- [x] Root cause: VarBoundProofs weren't copied when assigning from another variable
- [x] Fix: Added proof copying at lines 2697-2711 in VisitVarDecl
- [x] Tests: `361_guard_with_index_assignment_pass.cpp` (2 functions), `361b_assignment_outside_guard_fail.cpp`

## Phase 4: Remaining xfail Tests (32 tests)
These are known gaps that still need fixes:
137, 142, 200, 214, 310, 311, 319, 322, 324, 326, 327, 328, 331, 334, 335, 336, 338, 340, 341, 342, 343, 345, 346, 347, 348, 349, 350, 351, 354, 356, 432, 492

## Current Status
**Phase:** 3 Complete
**Tests:** 91% pass (315/347), 32 remaining xfail gaps
**Last Updated:** 2026-01-11
