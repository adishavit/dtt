# Analyzer Issue Catalog: 56 Detailed Cases

Each entry below includes: **Problem**, **Why it matters**, **Test**, and **Suggested fix**.

---

## 1) Guard proofs leak past `if` without `else`
**Problem:** Proofs from `if (cond)` remain active after the statement when no `else` exists.
**Why it matters:** Unsoundly treats `cond` as always true, allowing unsafe accesses.
**Test:** `examples/308_if_guard_persists_silent.cpp`.
**Suggested fix:** Remove proofs after the `then` block unless there is an `else` merge.

## 2) Lost-tracking state leaks across functions
**Problem:** `FieldStates`/`WarnedLostTracking` persist between function bodies.
**Why it matters:** Variables in later functions inherit unrelated “lost tracking.”
**Test:** `examples/309_lost_tracking_leak_xfail.cpp`.
**Suggested fix:** Clear tracking state on function entry alongside `Env`/`VarBoundProofs`.

## 3) Subtraction in constraints mis-parsed
**Problem:** `arg0 - 1 < _size` is parsed as `arg0 < _size`.
**Why it matters:** Valid constraints are misinterpreted.
**Test:** `examples/310_minus_expr_constraint_xfail.cpp`.
**Suggested fix:** Parse all arithmetic expressions with a real grammar.

## 4) Numeric `!= 0` misclassified as nullability
**Problem:** `arg0 != 0` is treated as a pointer null check.
**Why it matters:** Integer constraints yield wrong diagnostics.
**Test:** `examples/311_nonzero_constraint_xfail.cpp`.
**Suggested fix:** Only map `!= 0` to nullability for pointer-typed LHS.

## 5) Parenthesized constraints ignored
**Problem:** `dtt:requires=(_size > 0)` isn’t parsed.
**Why it matters:** Constraints silently drop, hiding bugs.
**Test:** `examples/312_parenthesized_requires_silent.cpp`.
**Suggested fix:** Strip outer parentheses or parse expression before split.

## 6) `arg1` requires checked against `arg0`
**Problem:** Requires for arg1 are evaluated using arg0 path.
**Why it matters:** Non-first-argument constraints are wrong.
**Test:** `examples/313_arg1_requires_xfail.cpp`.
**Suggested fix:** Use argN path when constraint targets argN.

## 7) Method ensures with `arg0._size` not substituted
**Problem:** Method postconditions don’t substitute `arg0._field`.
**Why it matters:** Postconditions depending on other args fail.
**Test:** `examples/314_method_postcond_arg_field_xfail.cpp`.
**Suggested fix:** Populate substitution map with `argN._field` for methods.

## 8) Guard comparison reversed not recognized ✅ FIXED
**Problem:** `v._size > i` doesn't create `i < _size` proof.
**Why it matters:** Equivalent guards are missed.
**Test:** `examples/315_guard_reversed_comparison_pass.cpp`.
**Suggested fix:** Normalize comparisons by swapping sides and flipping ops.
**Status:** Already implemented - reversed comparisons are normalized in extractVarGuard.

## 9) Guard RHS expression ignored
**Problem:** `i < v._size - 1` doesn’t establish bounds proof.
**Why it matters:** Common safe checks are ignored.
**Test:** `examples/316_guard_rhs_expression_xfail.cpp`.
**Suggested fix:** Allow simple arithmetic RHS expressions in guard extraction.

## 10) Temporary method calls skip constraints
**Problem:** Methods on temporaries have no object path and skip checks.
**Why it matters:** Unsafe temporary accesses go undetected.
**Test:** `examples/317_temporary_method_call_silent.cpp`.
**Suggested fix:** Create stable IDs for temporaries or track via Expr IDs.

## 11) Runtime index args in method preconditions skipped
**Problem:** `arg0 < _size` isn’t validated for dynamic indices without proofs.
**Why it matters:** Dynamic OOB accesses are silent.
**Test:** `examples/318_method_index_runtime_silent.cpp`.
**Suggested fix:** Emit Category C/D when runtime arg has no proof.

## 12) While-loop guards not tracked
**Problem:** Var-bound proofs are only for `for` loops, not `while`.
**Why it matters:** Many loops don’t establish bounds proofs.
**Test:** `examples/319_while_loop_guard_xfail.cpp`.
**Suggested fix:** Add proof extraction for `WhileStmt`.

## 13) Early-return index guard not propagated ✅ FIXED
**Problem:** `if (i >= size) return;` doesn't prove `i < size` afterward.
**Why it matters:** Safe patterns flagged as unsafe.
**Test:** `examples/320_early_return_index_guard_pass.cpp`.
**Suggested fix:** Treat early-return as else-branch proof.
**Status:** Fixed - Added `VarGuardInfo` early return handling in `TraverseIfStmt`.

## 14) Void postconditions don’t substitute arg values
**Problem:** `ensures=result._size == arg1` doesn’t substitute arg1.
**Why it matters:** Output-parameter postconditions are ineffective.
**Test:** `examples/321_void_postcond_arg_value_xfail.cpp`.
**Suggested fix:** Substitute argument values when applying void postconditions.

## 15) Compound null guard not recognized
**Problem:** `ptr && ptr->ready` doesn’t propagate `ptr != nullptr`.
**Why it matters:** Common safe pattern flagged as unknown.
**Test:** `examples/322_compound_null_guard_xfail.cpp`.
**Suggested fix:** Model short-circuit `&&` and apply proof in then-branch.

## 16) Double-negation guard not recognized ✅ FIXED
**Problem:** `!(ptr == nullptr)` doesn't imply non-null.
**Why it matters:** Equivalent forms don't work.
**Test:** `examples/323_double_negation_guard_pass.cpp`.
**Suggested fix:** Normalize negated equality to inequality.
**Status:** Already implemented - negation is handled in extractAllGuards.

## 17) Early-return null guard not propagated
**Problem:** `if (!ptr) return;` doesn’t mark non-null afterwards.
**Why it matters:** Common pattern fails.
**Test:** `examples/324_early_return_null_guard_xfail.cpp`.
**Suggested fix:** Propagate proof after early return.

## 18) OR guard else-branch not handled
**Problem:** `if (ptr == nullptr || flag) return;` doesn’t imply non-null afterward.
**Why it matters:** Disjunctive guards lose proofs.
**Test:** `examples/325_or_guard_else_branch_xfail.cpp`.
**Suggested fix:** Propagate negated OR guard to fallthrough.

## 19) Expression constraint on `_size` uses wrong substitution key
**Problem:** `_size + 1` expression doesn’t resolve to current `_size` value.
**Why it matters:** Expression constraints on fields fail unexpectedly.
**Test:** `examples/326_expr_constraint_field_prefix_xfail.cpp`.
**Suggested fix:** Normalize field names consistently in substitution.

## 20) Bool postcondition on `arg1` ignored
**Problem:** Boolean postconditions are applied only to arg0.
**Why it matters:** Multi-arg guards fail.
**Test:** `examples/327_bool_postcond_arg1_xfail.cpp`.
**Suggested fix:** Apply postconditions to the correct argument path.

## 21) do-while guards not tracked
**Problem:** `do { ... } while (i < size)` doesn’t create proofs.
**Why it matters:** Loop form lacks proof propagation.
**Test:** `examples/328_do_while_guard_xfail.cpp`.
**Suggested fix:** Add proof extraction for `DoStmt`.

## 22) Range-for loops don’t establish proofs
**Problem:** Range-for provides no bounds proofs for index usage.
**Why it matters:** Mixed range-for/index patterns fail.
**Test:** `examples/329_range_for_guard_xfail.cpp`.
**Suggested fix:** Document unsupported or add heuristics.

## 23) Ternary guard not propagated
**Problem:** `Data* chosen = pick ? ptr : fallback;` doesn’t infer non-null.
**Why it matters:** Ternary selection is common.
**Test:** `examples/330_ternary_guard_xfail.cpp`.
**Suggested fix:** If both arms proven non-null, mark result non-null.

## 24) For-loop guard reversed not recognized
**Problem:** `for (i=0; v._size > i; ++i)` doesn’t create proofs.
**Why it matters:** Equivalent loop forms are missed.
**Test:** `examples/331_for_reversed_guard_xfail.cpp`.
**Suggested fix:** Normalize loop comparisons like if-guards.

## 25) For-loop guard RHS expression not handled
**Problem:** `i < v._size - 1` in loop is ignored.
**Why it matters:** Safe loops lose proofs.
**Test:** `examples/332_for_rhs_expression_xfail.cpp`.
**Suggested fix:** Allow simple arithmetic RHS in loop condition analysis.

## 26) Void postcondition arg expression not substituted
**Problem:** `ensures=result._size == arg1 + 1` doesn’t substitute args.
**Why it matters:** Expressive postconditions are ineffective.
**Test:** `examples/333_postcond_arg_value_expr_xfail.cpp`.
**Suggested fix:** Substitute arg expressions in void postconditions.

## 27) Expr constraints on free functions ignore arg values
**Problem:** `arg0 + arg1 < 10` is not evaluated with actual arguments.
**Why it matters:** Expression constraints fail at call sites.
**Test:** `examples/334_call_expr_constraint_xfail.cpp`.
**Suggested fix:** Substitute arg values into expression constraints before checking.

## 28) Guards on return values don’t propagate proofs
**Problem:** `if (sizeOf(v) > 0)` doesn’t prove `_size > 0`.
**Why it matters:** Return-value guards are common.
**Test:** `examples/335_guard_on_return_value_xfail.cpp`.
**Suggested fix:** Map guards on return values to field proofs when `dtt:returns` exists.

## 29) Guards on return value expressions not propagated
**Problem:** `if (v.size() - 1 >= 0)` doesn’t yield `_size > 0` proof.
**Why it matters:** Simple expressions on return values are ignored.
**Test:** `examples/336_return_value_expr_guard_xfail.cpp`.
**Suggested fix:** Use expression reasoning to derive proofs from return-value guards.

## 30) Guards on assignment expressions ignored
**Problem:** `(local = ptr) != nullptr` doesn’t mark `local` non-null.
**Why it matters:** Common assignment-check idiom fails.
**Test:** `examples/337_guard_assignment_chain_xfail.cpp`.
**Suggested fix:** Treat assignment expressions as guard LHS and set proof for LHS.

## 31) Guards on array subscript paths with runtime indices ignored
**Problem:** `arr[0]._size` path is dropped due to runtime index handling.
**Why it matters:** Arrays of tracked objects don’t propagate guards.
**Test:** `examples/338_array_subscript_guard_xfail.cpp`.
**Suggested fix:** Treat constant subscripts as stable paths.

## 32) VarBoundProofs not invalidated on swap
**Problem:** Proofs remain tied to pre-swap objects, becoming unsound.
**Why it matters:** Proof leak allows OOB after swap.
**Test:** `examples/339_swap_varbound_proof_silent.cpp`.
**Suggested fix:** Invalidate or swap VarBoundProofs on swap.

## 33) Guards on casted pointers not recognized
**Problem:** `static_cast<T*>(raw) != nullptr` doesn’t create proof.
**Why it matters:** Casts are common in low-level code.
**Test:** `examples/340_guard_on_cast_xfail.cpp`.
**Suggested fix:** Unwrap casts when extracting pointer guards.

## 34) Bool postconditions with expressions not propagated
**Problem:** `ensures_if_true=_size >= 2` is ignored in guard propagation.
**Why it matters:** Bool postconditions are limited to simple constants.
**Test:** `examples/341_method_bool_postcond_expr_xfail.cpp`.
**Suggested fix:** Allow expression RHS in bool postconditions.

## 35) Lambda guard bodies ignored ✅ FIXED
**Problem:** Lambda bodies aren't traversed, so guards inside aren't used.
**Why it matters:** Guarded accesses inside lambdas are flagged incorrectly.
**Test:** `examples/342_lambda_guard_capture_pass.cpp`.
**Suggested fix:** Analyze lambda bodies at call sites or treat them as separate scopes.
**Status:** Fixed - Early return guard fix also resolved lambda guard capture.

## 36) Lambda early-return guard ignored ✅ FIXED
**Problem:** Early-return null guard in lambda doesn't establish proof.
**Why it matters:** Common callback pattern fails.
**Test:** `examples/343_lambda_guard_early_return_pass.cpp`.
**Suggested fix:** Traverse lambda bodies during invocation.
**Status:** Fixed - Early return guard fix also resolved lambda early return.

## 37) Nested member guard unresolved
**Problem:** Guards on `o.inner._size` don’t satisfy requires on `inner._size`.
**Why it matters:** Nested-object constraints fail.
**Test:** `examples/345_multilevel_member_guard_xfail.cpp`.
**Suggested fix:** Support multi-level member paths in guard extraction.

## 38) Unsigned comparison guards not recognized
**Problem:** `i < static_cast<unsigned>(_size)` doesn’t create proof.
**Why it matters:** Unsigned indices are common.
**Test:** `examples/346_guard_with_unsigned_comparison_xfail.cpp`.
**Suggested fix:** Allow integral casts and unsigned comparisons.

## 39) Casted index guards not recognized
**Problem:** `static_cast<int>(i) < _size` ignored.
**Why it matters:** Type-cast patterns are common.
**Test:** `examples/347_guard_with_casted_index_xfail.cpp`.
**Suggested fix:** Strip casts when extracting var-guard LHS.

## 40) Function-call guards on index not recognized
**Problem:** `abs(i) < _size` yields no proof.
**Why it matters:** Sanitized indices not accepted.
**Test:** `examples/348_guard_with_abs_call_xfail.cpp`.
**Suggested fix:** Require annotations for pure helper functions or allow simple call patterns.

## 41) Guards on reference parameters not recognized
**Problem:** Reference params in guard LHS aren’t treated as variables.
**Why it matters:** Many index parameters are references.
**Test:** `examples/349_guard_with_reference_param_xfail.cpp`.
**Suggested fix:** Treat ref `DeclRefExpr` like local vars.

## 42) Guards using struct fields as index not recognized
**Problem:** `s.i < _size` fails because LHS isn’t a plain var.
**Why it matters:** Index stored in state struct is common.
**Test:** `examples/350_guard_with_struct_field_xfail.cpp`.
**Suggested fix:** Accept `MemberExpr` LHS for var-bound proofs.

## 43) Guards on array element size not propagated
**Problem:** `arr[0]._size` guard doesn’t help element access.
**Why it matters:** Arrays of tracked objects are common.
**Test:** `examples/351_guard_with_array_size_xfail.cpp`.
**Suggested fix:** Treat constant indices as stable paths or track array elements.

## 44) Guards on pointer member fields don’t propagate non-null
**Problem:** `if (h.ptr != nullptr)` doesn’t mark `h.ptr` non-null.
**Why it matters:** Pointer members are prevalent.
**Test:** `examples/352_guard_on_pointer_member_xfail.cpp`.
**Suggested fix:** Support member-expression pointer guards.

## 45) Guards on pointer aliases don’t propagate non-null
**Problem:** `alias` doesn’t inherit proof from `ptr` even if assigned.
**Why it matters:** Alias patterns are common.
**Test:** `examples/353_guard_with_pointer_alias_xfail.cpp`.
**Suggested fix:** Propagate `_nonnull` proofs on pointer assignment.

## 46) Guards with constexpr bounds not tied to `_size`
**Problem:** `i < kSize` doesn’t imply `i < _size` even when `_size == kSize`.
**Why it matters:** Fixed-size patterns lack proofs.
**Test:** `examples/354_guard_with_constexpr_bound_xfail.cpp`.
**Suggested fix:** Track equality between `_size` and constants.

## 47) Chained comparison guards not recognized
**Problem:** `0 <= i && i < _size` only partially recognized.
**Why it matters:** Standard bounds check not fully used.
**Test:** `examples/356_guard_with_comparison_chain_xfail.cpp`.
**Suggested fix:** Extract upper-bound proof from compound guards.

## 48) Member-call boolean guards without annotations
**Problem:** `if (h.hasValue())` provides no proof.
**Why it matters:** Bool-returning guard methods are common.
**Test:** `examples/358_guard_with_member_call_bool_xfail.cpp`.
**Suggested fix:** Require `ensures_if_true` annotations and propagate when present.

## 49) OR-guard chain after early return doesn’t propagate proof
**Problem:** `if (size==0 || flag) return;` doesn’t imply `_size > 0` later.
**Why it matters:** Common early-return guard is ignored.
**Test:** `examples/360_guard_with_logical_or_chain_xfail.cpp`.
**Suggested fix:** Apply negated OR guard to fallthrough.

## 50) Index assignment inside guard doesn't inherit proof
**Problem:** `int j = i; v[j]` under guard doesn't propagate proof to `j`.
**Why it matters:** Simple index aliasing loses proof.
**Test:** `examples/361_guard_with_index_assignment_xfail.cpp`.
**Suggested fix:** Propagate var-bound proofs through assignments.

---

## Issues Discovered from SumatraPDF Analysis (2026-01-13)

## 51) Mutually exclusive enum/integer branches not tracked (HIGH PRIORITY)
**Problem:** `if (cmdId == X) { ptr = null; } if (cmdId == Y) { *ptr; }` warns even though X≠Y.
**Why it matters:** Switch-like if-else chains generate many false positives. This is the ROOT CAUSE of most SumatraPDF FPs.
**Test:** TBD - `examples/751_enum_mutual_exclusion_xfail.cpp`.
**Suggested fix:** Track enum/integer equality constraints to prune impossible paths.
**Source:** SumatraPDF Commands.cpp:489-510, CryptoUtil.cpp:82 (if-else chain reassignment)
**Complexity:** HIGH - requires path-sensitive analysis or equality constraint tracking.

## 52) Pre-increment in while condition not tracked ✅ FIXED
**Problem:** `while (++i <= n) { arr[i-1]; }` warns about index -1.
**Why it matters:** Pre-increment ensures `i >= 1` in loop body, but DTT doesn't track this.
**Test:** `examples/752_pre_increment_while_guard_pass.cpp`.
**Suggested fix:** Handle pre/post-increment in loop conditions to establish bounds.
**Source:** SumatraPDF GlobalPrefs.cpp:55-60 - fieldCount pre-increment.
**Status:** Fixed - Added `extractPreIncDecGuard` helper and handling in `TraverseWhileStmt`.

## 53) For-loop initializer assignment not tracked ✅ FIXED
**Problem:** `for (p = val; *p; p++)` warns about null even though `p` is assigned.
**Why it matters:** For-init assignments should establish non-null when assigned from non-null.
**Test:** `examples/753_for_init_assignment_pass.cpp`.
**Suggested fix:** Track variable reassignment in for-init before first use in condition/body.
**Status:** Already implemented - for-init declarations properly track assigned values.
**Note:** SumatraPDF CryptoUtil.cpp:82 was misattributed to this issue. The actual cause is Issue #51 (path insensitivity in if-else chains).

## 54) Intentional crash patterns (CrashMe) not suppressible
**Problem:** `void CrashMe() { char* p = nullptr; *p = 0; }` warns about null dereference.
**Why it matters:** Debug/assert code intentionally crashes; these are not bugs.
**Test:** TBD - `examples/7xx_intentional_crash_silent.cpp`.
**Suggested fix:** Add annotation to suppress or detect crash-intent patterns (e.g., function named "Crash*").
**Source:** SumatraPDF BaseUtil.h:205 - CrashMe() function.

## 55) If-else branch assignment not tracked
**Problem:** `T* p; if (c) p = f1(); else p = f2(); if (!p)` warns "uninitialized".
**Why it matters:** Variable assigned in all branches should not be "uninitialized".
**Test:** TBD - `examples/7xx_if_else_branch_assignment_xfail.cpp`.
**Suggested fix:** Track that variable is assigned in all branches of if-else.
**Source:** SumatraPDF Vec.h:55, TextSearch.cpp:283 - newEls/found patterns.

## 56) Placement new not tracked as non-deletable ✅ FIXED
**Problem:** `Foo* p = new (buffer) Foo(); delete p;` doesn't warn about mismatched deallocation.
**Why it matters:** Placement new constructs in pre-allocated memory; delete/free is UB.
**Test:** `examples/754_placement_new_no_delete_fail.cpp`.
**Suggested fix:** Track `AllocSource::Placement` for placement new (not nothrow new), warn on delete/free.
**Status:** Fixed - Added `isPlacementNew()` helper to distinguish from nothrow new, updated all `CXXNewExpr` handling paths.

