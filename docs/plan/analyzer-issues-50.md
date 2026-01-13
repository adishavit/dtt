# Analyzer Issue Catalog: Remaining Gaps

This document tracks only **unfixed gaps** in DTT. Issues are removed when fixed.

---

## 7) Method ensures with `arg0._size` not substituted
**Problem:** Method postconditions don't substitute `arg0._field`.
**Why it matters:** Postconditions depending on other args fail.
**Test:** `examples/314_method_postcond_arg_field_xfail.cpp`.
**Suggested fix:** Populate substitution map with `argN._field` for methods.

## 14) Void postconditions don't substitute arg values
**Problem:** `ensures=result._size == arg1` doesn't substitute arg1.
**Why it matters:** Output-parameter postconditions are ineffective.
**Test:** `examples/321_void_postcond_arg_value_xfail.cpp`.
**Suggested fix:** Substitute argument values when applying void postconditions.

## 22) Range-for loops don't establish proofs
**Problem:** Range-for provides no bounds proofs for index usage.
**Why it matters:** Mixed range-for/index patterns fail.
**Test:** `examples/329_range_for_guard_xfail.cpp`.
**Suggested fix:** Document unsupported or add heuristics.

## 23) Ternary guard not propagated (PARTIAL)
**Problem:** `Data* chosen = pick ? ptr : fallback;` doesn't infer non-null when both operands are guarded variables.
**Why it matters:** Ternary selection is common.
**Test:** `examples/330_ternary_guard_xfail.cpp`.
**Suggested fix:** Lookup varNullability in inferNonnullFromExpr for DeclRefExprs.
**Status:** Works for literal non-null (address-of, new, string literals). Gap: guarded variables.

## 26) Void postcondition arg expression not substituted
**Problem:** `ensures=result._size == arg1 + 1` doesn't substitute args.
**Why it matters:** Expressive postconditions are ineffective.
**Test:** `examples/333_postcond_arg_value_expr_xfail.cpp`.
**Suggested fix:** Substitute arg expressions in void postconditions.

## 48) Member-call boolean guards without annotations
**Problem:** `if (h.hasValue())` provides no proof.
**Why it matters:** Bool-returning guard methods are common.
**Test:** `examples/358_guard_with_member_call_bool_xfail.cpp`.
**Suggested fix:** Require `ensures_if_true` annotations and propagate when present.

---

## Issues Discovered from SumatraPDF Analysis

## 51) Mutually exclusive enum/integer branches not tracked (HIGH PRIORITY)
**Problem:** `if (cmdId == X) { ptr = null; } if (cmdId == Y) { *ptr; }` warns even though X≠Y.
**Why it matters:** Switch-like if-else chains generate many false positives. ROOT CAUSE of most SumatraPDF FPs.
**Test:** TBD - `examples/751_enum_mutual_exclusion_xfail.cpp`.
**Suggested fix:** Track enum/integer equality constraints to prune impossible paths.
**Complexity:** HIGH - requires path-sensitive analysis or equality constraint tracking.

## 54) Intentional crash patterns (CrashMe) not suppressible
**Problem:** `void CrashMe() { char* p = nullptr; *p = 0; }` warns about null dereference.
**Why it matters:** Debug/assert code intentionally crashes; these are not bugs.
**Test:** TBD.
**Suggested fix:** Add annotation to suppress or detect crash-intent patterns.

## 55) If-else branch assignment not tracked
**Problem:** `T* p; if (c) p = f1(); else p = f2(); if (!p)` warns "uninitialized".
**Why it matters:** Variable assigned in all branches should not be "uninitialized".
**Test:** TBD.
**Suggested fix:** Track that variable is assigned in all branches of if-else.

---

**Total remaining gaps: 9**
