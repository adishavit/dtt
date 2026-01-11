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
- `TraverseWhileStmt()` (line ~3543) - Handles while-loop proof propagation
- Lines ~4583-4614 - Free function constraint checking

## Phase 2: Rename Test (1 test)
- [x] **308** `_silent.cpp` → `_fail.cpp` (DTT correctly catches guard leak)

## Phase 3: Quick Wins - LOW Complexity (4 bugs)

### 313 - arg1 requires checked against arg0 path
- [x] Root cause: Constraint parsing didn't extract argN from "argN._field" format
- [x] Fix: Added parsing at lines 675-690, use correct argIdx at lines 4583-4592
- [x] Tests: `313_arg1_requires_pass.cpp`, `313b_arg1_requires_fail.cpp`

### 315 - Reversed comparison `v._size > i` not recognized
- [x] Root cause: `extractVarGuard()` only handled DeclRefExpr on LHS
- [x] Fix: Added reversed form handling at lines 3010-3092
- [x] Tests: `315_guard_reversed_comparison_pass.cpp`, `315b_guard_reversed_fail.cpp`

### 323 - Double negation `!(ptr == nullptr)` not recognized
- [x] Root cause: `extractGuard()` didn't recurse through `!` operator
- [x] Fix: Added recursive `!` handling at lines 3120-3134
- [x] Tests: `323_double_negation_guard_pass.cpp`, `323b_double_negation_fail.cpp`

### 361 - Assignment `int j = i;` loses proof
- [x] Root cause: VarBoundProofs weren't copied when assigning
- [x] Fix: Added proof copying at lines 2697-2711 in VisitVarDecl
- [x] Tests: `361_guard_with_index_assignment_pass.cpp`, `361b_assignment_outside_guard_fail.cpp`

## Phase 4: Loop Guards & Proof Scoping (5 bugs)

### 310 - Minus expression constraint parsing
- [x] Status: Already fixed (arithmetic in constraints works)
- [x] Tests: `310_minus_expr_constraint_pass.cpp`, `310_minus_expr_constraint_fail.cpp`

### 311 - Nonzero constraint on int
- [x] Root cause: `arg0 != 0` on int treated as nullptr check
- [x] Fix: Added type check at lines 4625-4646 before nullptr semantics
- [x] Tests: `311_nonzero_constraint_pass.cpp`, `311_nonzero_constraint_fail.cpp`

### 319 - While-loop guard
- [x] Root cause: No `TraverseWhileStmt` handler
- [x] Fix: Added `TraverseWhileStmt` at lines 3543-3571
- [x] Tests: `319_while_loop_guard_pass.cpp` (6 variants), `319_while_loop_guard_fail.cpp` (3 variants)

### 319b - Wrong-object proof scoping
- [x] Root cause: Guard on v1._size incorrectly proved v2[i]
- [x] Fix: Require matching proof when variable has proofs (lines 4230-4270)
- [x] Tests: Included in `319_while_loop_guard_fail.cpp`

### 328 - Do-while guard
- [x] Status: Correct behavior - do-while body runs before condition
- [x] Relies on constant evaluation for first iteration safety
- [x] Tests: `328_do_while_guard_pass.cpp` (3 variants), `328_do_while_guard_fail.cpp` (3 variants)

## Phase 5: Already Working (Converted from xfail)

### 322 - Compound null guards
- [x] Status: Already working (ptr && ptr->ready)
- [x] Tests: `322_compound_null_guard_pass.cpp` (4 variants), `322_compound_null_guard_fail.cpp`

### 324 - Early-return null guard + else-return fix
- [x] Root cause: `if (ptr) {...} else {return;}` didn't propagate proof
- [x] Fix: Added else-termination handling at lines 3461-3464
- [x] Tests: `324_early_return_null_guard_pass.cpp` (10 variants), `324_early_return_null_guard_fail.cpp`

### 326 - Field expression constraints
- [x] Status: Already working (_size + 1 > 0)
- [x] Tests: `326_expr_constraint_field_prefix_pass.cpp`, `326_expr_constraint_field_prefix_fail.cpp`

### 327 - Bool postcondition on arg1
- [x] Status: Already working (ensures_if_true=arg1._field)
- [x] Tests: `327_bool_postcond_arg1_pass.cpp`, `327_bool_postcond_arg1_fail.cpp`

### 331 - For-loop reversed guard
- [x] Status: Already working (v._size > i in for-loop)
- [x] Tests: `331_for_reversed_guard_pass.cpp`, `331_for_reversed_guard_fail.cpp`

### 338 - Array subscript guards
- [x] Status: Already working (arr[N]._field paths)
- [x] Tests: `338_array_subscript_guard_pass.cpp`, `338_array_subscript_guard_fail.cpp`

### 340 - Cast guards
- [x] Status: Already working (static_cast, reinterpret_cast, C-style)
- [x] Tests: `340_guard_on_cast_pass.cpp`, `340_guard_on_cast_fail.cpp`

## Phase 6: More Already Working (Converted from xfail)

### 214 - Move init vs assign
- [x] Status: Already working (move construction handled)
- [x] Tests: `214_move_init_vs_assign_pass.cpp` (3 variants), `214_move_init_vs_assign_fail.cpp`

### 346 - Unsigned comparison guard
- [x] Status: Already working (casts unwrapped in extractVarGuard)
- [x] Tests: `346_guard_with_unsigned_comparison_pass.cpp` (5 variants), `346_guard_with_unsigned_comparison_fail.cpp`

### 347 - Casted index guard
- [x] Status: Already working (casts handled)
- [x] Tests: `347_guard_with_casted_index_pass.cpp` (5 variants), `347_guard_with_casted_index_fail.cpp`

### 349 - Reference param guard
- [x] Status: Already working (reference params work like values)
- [x] Tests: `349_guard_with_reference_param_pass.cpp` (7 variants), `349_guard_with_reference_param_fail.cpp`

### 356 - Comparison chain guard
- [x] Status: Already working (extractVarGuard handles &&)
- [x] Tests: `356_guard_with_comparison_chain_pass.cpp` (6 variants), `356_guard_with_comparison_chain_fail.cpp`

## Phase 7: Even More Already Working (Converted from xfail)

### 200 - Rvalue tracking
- [x] Status: Already working (fixed by 214 move construction)
- [x] Tests: `200_rvalue_tracking_pass.cpp` (4 variants), `200_rvalue_tracking_fail.cpp`

### 334 - Call expr constraint
- [x] Status: Already working (arg substitution in expressions)
- [x] Tests: `334_call_expr_constraint_pass.cpp` (4 variants), `334_call_expr_constraint_fail.cpp`

### 335 - Guard on return value
- [x] Status: Already working (return value guards propagate)
- [x] Tests: `335_guard_on_return_value_pass.cpp` (4 variants), `335_guard_on_return_value_fail.cpp`

### 336 - Return value expr guard
- [x] Status: Already working (expression guards on return values)
- [x] Tests: `336_return_value_expr_guard_pass.cpp` (4 variants), `336_return_value_expr_guard_fail.cpp`

### 341 - Method bool postcond expr
- [x] Status: Already working (expression postconditions)
- [x] Tests: `341_method_bool_postcond_expr_pass.cpp` (4 variants), `341_method_bool_postcond_expr_fail.cpp`

### 345 - Multilevel member guard
- [x] Status: Already working (nested path resolution)
- [x] Tests: `345_multilevel_member_guard_pass.cpp` (4 variants), `345_multilevel_member_guard_fail.cpp`

### 348 - Abs call guard
- [x] Status: Already working (function call guards)
- [x] Tests: `348_guard_with_abs_call_pass.cpp` (3 variants), `348_guard_with_abs_call_fail.cpp`

### 350 - Struct field index
- [x] Status: Already working (MemberExpr as index)
- [x] Tests: `350_guard_with_struct_field_pass.cpp` (4 variants), `350_guard_with_struct_field_fail.cpp`

### 351 - Array size guard
- [x] Status: Already working (operator[] on array elements)
- [x] Tests: `351_guard_with_array_size_pass.cpp` (4 variants), `351_guard_with_array_size_fail.cpp`

### 354 - Constexpr bound
- [x] Status: Partially working (if guards work, for loops need direct _size ref)
- [x] Tests: `354_guard_with_constexpr_bound_pass.cpp` (6 variants), `354_guard_with_constexpr_bound_fail.cpp`

## Phase 8: Final Remaining Gaps (5 tests - all HIGH complexity)

### Nullptr/Pointer
- [ ] 137 - Pointer arithmetic (expression-level tracking, ~1500 lines)
- [ ] 142 - Complex null guards (multiple features needed)

### Lambdas
- [ ] 342 - Lambda guard capture (lambda scope handling)
- [ ] 343 - Lambda guard early return (lambda traversal)

### Other
- [ ] 432 - Buffer ternary (min-size tracking through ternary)

## Current Status
**Phase:** 7 Complete, 8 In Progress
**Tests:** 99% pass (360/365 total), 5 remaining xfail gaps
**Last Updated:** 2026-01-11
