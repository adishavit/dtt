# DTT Gap Closure Plan

Consolidated view of all known gaps and implementation plans.

For the current implementation repair pass and deferred work, see
[Implementation review status](plans/IMPLEMENTATION_REVIEW_STATUS.md).
The capability roadmap below is historical; verify its individual closure claims
against the current code and tests before starting work.

---

## Summary

The tables below are historical feature notes, not measured coverage. Run CTest
and inspect individual diagnostics to establish current status. Known gaps and
false positives are represented by `_xfail` and `_xpass` fixtures.

**Priority:** P0=Critical, P1=High, P2=Medium, P3=Future;
FP1=High-impact, FP2=Medium, FP3=Low.

---

## Recently Closed

| Gap | Test | Branch |
|-----|------|--------|
| **Auto resource leak detection** | `1012_auto_resource_leak_fail.cpp` | main |
| **Resource-centric tracking (v34)** | `1013_auto_resource_pass.cpp` | main |
| **C library annotations (40+ functions)** | `988_fopen_patterns_pass.cpp` | main |
| Auto-postcondition: arithmetic | `1014_auto_postcond_arithmetic_fail.cpp` | main |
| Auto-postcondition: nested fields | `1027_auto_postcond_nested_complex_fail.cpp` | main |
| Auto-postcondition: param names | `1027_auto_postcond_nested_complex_fail.cpp` | main |
| Malloc/free comprehensive | `839_malloc_fail.cpp` | session-vs-linear-types |
| Fopen/fclose comprehensive | `841_fopen_fail.cpp` | session-vs-linear-types |
| Strcpy/strncpy CVE patterns | `813_strcpy_fail.cpp` | session-vs-linear-types |
| Buffer over-read CWE-126 | `850_buffer_overread_fail.cpp` | session-vs-linear-types |
| Early return proof preservation | `1016_early_return_proof_pass.cpp` | session-vs-linear-types |
| Transitive preconditions | `950_transitive_precond_pass.cpp` | dual-mechanisms |
| Counterexample generation | `951_refutation_pass.cpp` | dual-mechanisms |
| Frame inference | `952_frame_inference_pass.cpp` | dual-mechanisms |
| Transitive expr: ptr arithmetic | `953_transitive_expr_pass.cpp` | main |
| Transitive expr: size exprs | `954_z3_evaluation_pass.cpp` | main |
| Transitive expr: member access | `955_member_path_pass.cpp` | main |
| Z3 SMT evaluation | `954_z3_evaluation_pass.cpp` | main |
| std::expected monadic | `999_expected_monadic_pass.cpp` | main |
| Value Concepts | `983_symbolic_bounds_pass.cpp` | main |
| Nested member navigation | `956_transitive_advanced_pass.cpp` | main |
| Member + offset | `955_member_path_pass.cpp` | main |
| Generic pointer-arg UAF check | `1620_generic_uaf_fail.cpp` | PR #23 |
| DIR* null checking | `1330_posix_dir_ops_pass/fail.cpp` | PR #23 |
| Adapter pop() _size tracking | `1600_adapter_pop_size_fail.cpp` | PR #23 |
| Sequence at() bounds checking | `1610_sequence_at_bounds_fail.cpp` | PR #23 |
| pop_front() _size tracking | `1630_pop_front_size_fail.cpp` | PR #23 |
| mkstemp/epoll resource tracking | `1360_posix_resource_pass/fail.cpp` | PR #23 |
| Pthread lifecycle tracking | `1400_pthread_lifecycle_pass.cpp` | PR #23 |
| `&expr` nonnull inference | `1100_addr_of_nonnull_pass.cpp` | main (PR #36) |
| Arity mismatch diagnostic | `1101_arity_mismatch_fail.cpp` | main (PR #36) |
| `.dtt` postcondition ordering | external lib tests | main (PR #36) |
| External library test suite | `tests/external/` | main (PR #36) |
| `.dtt` frame semantics (hasDTTAnnotations) | `1102_dtt_frame_semantics_pass.cpp` | fix/dtt-frame-semantics |
| Deprecated bare `dtt:field=value` syntax removed | `01_basic_fail.cpp`, `02_basic_pass.cpp` | main |
| Test initialization: struct members properly initialized | 53 test files | main |
| Memory functions (memset/memcpy/memmove) as initializers | `726_memory_functions_pass.cpp` | main |
| freopen null proof invalidation | `840_fopen_freopen_fail.cpp` | main |
| sizeof unevaluated (FP1) | `1701_sizeof_unevaluated_pass.cpp` | main |
| Inline asm output (FP2) | `1700_asm_output_operands_pass.cpp` | main |
| Out-parameter init (FP3) | `1702_out_param_init_pass.cpp` | main |
| Ownership transfer return (FP4) | `1703_ownership_transfer_return_pass.cpp` | main |
| Bitfield width (FP6) | `1705_bitfield_width_pass.cpp` | main |
| Modular invariants (FP7) | `1706_modular_invariant_pass.cpp` | main |
| Negative signed index (FP9) | `1708_negative_signed_index_pass.cpp` | main |
| Win32 HANDLE deferred guards (1300) | `1300_win_handle_createfile_pass.cpp` | main |
| Integer fd deferred guards (1360) | `1360_posix_resource_pass.cpp` | main |
| Multi-fn `.dtt` syntax + DRY libc.dtt | `annotations/libc.dtt` | main |

---

## False Positives (xpass Tests)

Discovered by scanning real-world C codebases (libwebp, giflib) with DTT. These are
safe code patterns that DTT incorrectly rejects. Each class documents a category of
false positive and the `_xpass` test that captures it.

**xpass semantics:** Mirror of xfail. Uses `WILL_FAIL TRUE` in CMake. When FP exists,
DTT fires → compilation fails → test PASSES. When FP is fixed, DTT stays silent →
compilation succeeds → test FAILS → rename to `_pass.cpp`.

### Summary

| # | FP Class | Test | Priority | Impact | Source | Status |
|---|----------|------|----------|--------|--------|--------|
| 1 | sizeof unevaluated | `1701_sizeof_unevaluated_pass.cpp` | FP1 | Medium | libwebp | **CLOSED** |
| 2 | Inline asm output operands | `1700_asm_output_operands_pass.cpp` | FP2 | Low | libwebp | **CLOSED** |
| 3 | Out-parameter initialization | `1702_out_param_init_pass.cpp` | FP1 | High | libwebp, giflib | **CLOSED** |
| 4 | Ownership transfer via return | `1703_ownership_transfer_return_pass.cpp` | FP1 | High | libwebp, giflib | **CLOSED** |
| 4b | Ownership transfer (fd) | `1703b_ownership_transfer_fd_xpass.cpp` | FP1 | High | giflib | Open |
| 5 | Unchecked pointer parameters | `1704_unchecked_param_xpass.cpp` | FP3 | Very High | libwebp, giflib | Partial |
| 6 | Bitfield width constraints | `1705_bitfield_width_pass.cpp` | FP2 | Low | libwebp | **CLOSED** |
| 7 | Modular/wrapping invariants | `1706_modular_invariant_pass.cpp` | FP2 | Low | libwebp, giflib | **CLOSED** |
| 8 | Cross-function bounds validation | `1707_cross_func_bounds_xpass.cpp` | FP1 | High | libwebp | Open |
| 9 | Negative signed index | `1708_negative_signed_index_pass.cpp` | FP2 | Low | libwebp | **CLOSED** |

### FP1: sizeof Unevaluated Operands — CLOSED

**Test:** `1701_sizeof_unevaluated_pass.cpp` (was `_xpass`)

**Fix:** `TraverseUnaryExprOrTypeTraitExpr` override in DTTPlugin.cpp (line ~7329)
returns `true` without traversing into the operand. sizeof constants still evaluated
via `decomposeSymValue()` and `tryEvalInt()`.

### FP2: Inline Asm Output Operands — CLOSED

**Test:** `1700_asm_output_operands_pass.cpp` (was `_xpass`)

**Fix:** `VisitGCCAsmStmt` parses output constraints (`=` and `+` prefixes)
and marks corresponding variables as initialized.

### FP3: Out-Parameter Initialization — CLOSED

**Test:** `1702_out_param_init_pass.cpp` (was `_xpass`)

**Fix:** Out-parameter initialization heuristic in `VisitCallExpr`: when `&local_var` is
passed to a non-const `T*` parameter, mark the local as initialized. Covers scalars and
structs. The heuristic assumes the callee writes through the pointer (standard C/C++
out-parameter convention).

### FP4: Ownership Transfer via Return — CLOSED (pointer); fd variant open

**Test:** `1703_ownership_transfer_return_pass.cpp` (was `_xpass`)

**Fix:** DTT recognizes `return ptr` where `ptr` holds a tracked resource as ownership
transfer to the caller, clearing the leak obligation.

**Remaining:** `1703b_ownership_transfer_fd_xpass.cpp` — `if (fd == -1) return -1;`
does not prove `fd >= 0`, the guard on open()'s descriptor, so enforce mode reports fd
as never checked. The fdopen wrapper and the if/else exclusive open no longer report an
overwrite: one open() call registered its contract once per declared arity
(`1752_fd_resource_*`).

### FP5: Unchecked Pointer Parameters

**Test:** `1704_unchecked_param_xpass.cpp`

**Gap:** DTT assumes all pointer parameters could be null and requires null checks before
dereference. In practice, most internal/static functions have implicit non-null contracts.
This produces the largest volume of FPs (~700 in libwebp, ~60 in giflib).

```c
// Virtually every internal function in libwebp/giflib
static void ProcessRow(VP8Decoder* dec) {
    VP8MB* mb = dec->mb_info_;  // DTT: dec could be null
    int offset = mb->x_offset;  // DTT: mb could be null
    ...
}
```

**Solution:** Multiple approaches:
1. **Conservative:** Only warn on public API entry points, not static/internal functions
2. **Annotation-based:** `[[dtt::nonnull]]` or `__attribute__((nonnull))`
3. **Heuristic:** Skip null warnings for parameters of `static` functions
4. **Configurable severity:** Make unchecked-param a separate warning category users can
   suppress (e.g., `-Wno-dtt-unchecked-param`)

**Effort:** ~50-100 lines (for severity separation), much more for full solution

### FP6: Bitfield Width Constraints — CLOSED

**Test:** `1705_bitfield_width_pass.cpp` (was `_xpass`)

**Fix:** `exprToSymValueImpl` detects bitfield MemberExprs and encodes them as
`BitAnd(Var(name), (1<<width)-1)`. This leverages existing `upperBound()` which
returns the constant for BITAND nodes. Extended `upperBound()` and
`isGuaranteedNonNegative()` for ADD/MUL composites to handle arithmetic on
bitfield values (e.g., `segment*2 + skip`).

### FP7: Modular/Wrapping Invariants — CLOSED

**Test:** `1706_modular_invariant_pass.cpp` (was `_xpass`)

**Fix:** DTT now recognizes `x % N` and `x & MASK` as producing bounded results,
adding implicit proofs `0 <= result < N` (modulo) and `0 <= result <= MASK` (bitmask).
This covers index wrapping patterns common in hash tables and ring buffers.

### FP8: Cross-Function Bounds Validation

**Test:** `1707_cross_func_bounds_xpass.cpp`

**Gap:** When a value is validated in one function and used in another (callee), DTT
doesn't propagate the constraint across the call boundary.

```c
// libwebp src/dec/vp8l_dec.c:457
int color_cache_bits;
// Caller validates: 0 < color_cache_bits <= MAX_CACHE_BITS
VP8LColorCacheInit(&color_cache, color_cache_bits);
// Inside VP8LColorCacheInit:
//   1 << color_cache_bits  // DTT flags: shift could overflow
```

**Solution:** This is the interprocedural constraint propagation problem. Requires
either annotations (`[[dtt::requires(bits > 0 && bits <= 11)]]`) or summary-based
interprocedural analysis.

**Effort:** Large — ties into general interprocedural analysis (Section 6 above)

### FP9: Negative Signed Index — CLOSED

**Test:** `1708_negative_signed_index_pass.cpp` (was `_xpass`)

**Fix:** When Z3 finds a counterexample for `idx >= 0 && idx < size`, re-check with
only the upper bound (`idx < size`, treating index as unsigned). If upper bound passes,
suppress — the only violation is negative index, assumed non-negative by array index
convention. See `docs/HEURISTICS.md` #2.

---

## Known Limitations (Enhancement Opportunities)

### Transitive Propagation: Return Value Tracking

**Gap:** Function return values not tracked for buffer size propagation.

```cpp
static char* get_buffer(BufferHolder* h) {
    return h->buf;  // Returns pointer to 20-byte buffer
}

static void wrapper(BufferHolder* h, size_t n) {
    char* p = get_buffer(h);
    copy_n(p, "data", n);  // DTT cannot track p's origin
}
```

**Priority:** P2 (requires callee return analysis)

### Transitive Propagation: Ternary Conditionals

**Gap:** Path-sensitive analysis needed for ternary expressions.

```cpp
static void wrapper(char* buf, size_t n, size_t m, bool use_n) {
    copy_n(buf, "data", use_n ? n : m);  // DTT uses first argument regardless
}

void test_bug() {
    char small[10];
    wrapper(small, 5, 15, false);  // uses m=15, but DTT may not detect
}
```

**Priority:** P2 (requires path-sensitive analysis)

---

## P0: Critical (Blocks CVE Detection)

### 1. Union Discriminant Correlation

**Tests:** `1000_union_cve_discriminant_xfail.cpp`, `1002_union_cve_kernel_xfail.cpp`, `1004_union_cve_parser_xfail.cpp`

**Gap:** Cannot correlate tag field with union member access.

```cpp
struct TaggedUnion { int tag; union { int i; float f; } data; };
void process(TaggedUnion *t) {
    if (t->tag == TAG_INT)
        int x = t->data.i;  // DTT cannot prove safe
}
```

**Solution:**
1. Detect struct with enum/int + union
2. Track `struct._tag` → `struct.union._active` correlation
3. Extract proofs from `if (s.tag == CONSTANT)`

**Effort:** 300-400 lines, 2-3 weeks

---

## P1: High Priority

### 2. Switch Case Proofs (variant) — CLOSED

**Test:** `832g_variant_switch_pass.cpp` (was `_xpass`), `1754_switch_reach_*`.

Each case label starts from the state before the switch plus the case's fact:
`_value == k` for a variable or field, `_variant_index == k` for `v.index()` of a
`std::variant`; the default label knows the value differs from every case. Every
case is analyzed: inside a case the condition holds the case value, even where an
earlier known value disagreed, since known values are not exact yet.

### 3. Reference Invalidation — Out of Scope

**Test:** `832d_variant_ref_invalidation_silent.cpp`

Dangling references after variant reassignment. Requires `ref._depends_on` tracking.
Classified as `_silent` (out of scope for current analysis).

### 4. Cross-Function Union Tracking

**Tests:** `1002_union_cve_kernel_xfail.cpp`, `1004_union_cve_parser_xfail.cpp`

**Gap:** Union state lost when passed to function. Needs parameter annotation or
interprocedural analysis.

### 4b. Union Type Punning Detection

**Gap:** Pointer/integer confusion through unions (general case). Specific CVE patterns
are already caught (`1001_union_cve_ptr_int_fail`, `1005_union_cve_size_fail`), but
subtler type punning via reinterpretation is not detected.

**Priority:** P2

### 5. Control Flow — Partial

Else/early-exit guards and basic conditional-expression tracking exist. Ternary
literal non-null arms are covered by `330_ternary_guard_pass.cpp`; mixed-null arms
require a guard. Range-for does not relate an independent index to the iteration
count: the index grows each pass, so its accesses are "cannot verify", unsafe
(`329_range_for_bounds_fail.cpp`) or safe (`329_range_for_guard_pass.cpp`) alike.

A value written on only one branch is unknown after the branch
(`1742_one_sided_branch_write_*`); the merge used to keep the written value as
if both paths had it. A variable whose value differs between the paths keeps
the hull of their ranges, so the clamp `if (n < 0) n = 0;` establishes `n >= 0`
(`1742_clamp_after_branch_pass.cpp`).

Every `if` traverses each branch from the state before it and joins the
states of the branches that fall through (`1751_branch_state_*`). Facts and
releases from one branch no longer leak into the other branch or past the
`if`. A pointer freed on one path only is "cannot verify" at a later use or
free, not a proven use-after-free or double-free (700 variants 3 and 7, 701
variants 4 and 12). The branch whose condition is known (701 variant 12,
`bool error = true; if (error)`) is not yet chosen. Paths are not correlated: frees under
`if (c)` and `if (!c)` are "cannot verify" at the second free. Heap memory
initialized on one path only is not reported at a later read.

A path ends at `return`, `throw`, `break`, `continue`, `goto` and calls to
functions that do not return (`abort`, `exit`, `__builtin_unreachable`); code no
path reaches is not analyzed (`1754_switch_reach_*`). `break` sends the state to
the enclosing loop or switch, `goto` to its label, `throw` and calls that may
throw to the handlers of the enclosing `try`; a `throw` that leaves the function
checks release obligations like a `return`.

Conditions are evaluated in order, and each guard adds its facts where it is
evaluated (`1756_condition_facts_*`): the arms of `?:`, the right operand of `&&`
and `||`, and the branches of an `if` start from what the condition said on
that path, including assignments and dereferences inside the condition.
`!(a && b)` says nothing about `a` alone. Writing a field drops the proofs about
it that the new value does not preserve; assigning a variable drops its bounds.
A guard through a narrowing or signedness conversion gives only the facts that
hold for every value. A comparison with a variable or field that holds a known
constant on that path bounds like a comparison with the constant.

A loop's head joins the state on entry with the states its body passes back
(`1758_loop_state_*`): the body is explored until the head stops changing, then
traversed once more to report, and the state after the loop joins the paths
that leave it (the condition failing, `break`). `++`, `--`, `+= c` and `-= c`
shift a variable's value and bounds; at a loop head a bound that keeps moving
is dropped, so the iteration ends, and one more pass recovers the bounds each
pass re-establishes (a `do`-`while` condition). A head still changing after 32
passes forgets its values. A label that a later `goto` jumps back to is a loop
head the same way (`1759_goto_loop_*`); computed gotos are not followed. The
range a loop's shape gives its variable (`for (i = 0; i < n; i++)`, a pointer
stepping to an end pointer into the same array) applies in the body while the
body does not write the variable or the bound's operands and no jump enters the
body; a pointer the body steps exactly once per pass with `++p` is at its entry
offset plus the passes so far. A body whose facts contradict each other (a loop
that never runs) is still "cannot verify" (`1750_solver_contradiction_xpass.cpp`).
A pointer freed in one iteration is "cannot verify" at its use in the next, not
a proven use-after-free (700 variant 7, 701 variant 5). A global a called
function writes is not modeled yet (`1760_value_model_gaps_xfail.cpp`).

Historical fixtures also cover member call guards (358), buffer ternaries (432),
pointer aliases (135), complex null sources (144), and indirect initialization (145).
Their rejection alone does not establish all scenarios in each file are checked.

### 5c. Integer Values — Partial

Integer values have their type's width and signedness (`1765_integer_width_*`,
`1765_memory_size_width_fail.c`): constants are evaluated in their own type;
integer conversions, unsigned arithmetic, shifts and narrow `++`/`--` are wrapped
into the type (`SymValue::Wrap`, encoded exactly for the solver) unless the
operands' ranges keep the result in range. Arguments and `mem*` sizes are
checked as passed, converted to the parameter's type. A call-site size
requirement is the callee's bound expression with the arguments substituted;
a size that is not a function of the callee's parameters is "cannot verify" in
the callee rather than a requirement callers silently skip (`949`, `950`).
Signed `+`, `-`, `*` are assumed not to overflow (HEURISTICS.md #3).

Values flow (`1766_value_flow_*`): assignment from a temporary or a call takes
its value (or forgets the old one); a local reference bound to one object is
that object; a local whose address escapes is forgotten by writes through
pointers and by calls (HEURISTICS.md #5); a call forgets the globals the
caller names unless the callee writes none (HEURISTICS.md #4); a static local's
initializer is not its value on every call; `std::swap` swaps scalars' values;
a pointer copy carries its allocation's lifetime, and freeing either frees
both. A reference bound to a converted value or a temporary is its own object,
not an alias of the source; a narrowing copy does not keep what was proven of
its source; a swap moves an index's bounds (`i < v.size()`) with its value; an
unannotated operator writes the arguments it takes by non-const reference
(`std::cin >> n`); a call-site requirement is read over the definition's
parameters, whatever a redeclaration names them.

Not yet covered:
- A narrow loop counter that wraps is forgotten and its negative index is then
  accepted by HEURISTICS.md #2 (`1760` variant 1).
- A write to a bit-field is not reduced to the field's width; a read of it is
  "cannot verify" rather than its stored value.
- A compound assignment (`x /= y`, `x >>= y`) is computed in the left
  operand's type, not the promoted computation type; `+`, `-`, `*` agree, but
  mixed-signedness `/`, `%`, `>>` can differ.
- A conversion to `bool` has no value (unknown), not `x != 0`.
- A library call forgets escaped locals only when it is passed a pointer;
  reference parameters are detected from the argument's type (never a
  reference) instead of the parameter's, and a callable argument forgets
  globals but not escaped locals.
- Precision: a pointer that may come from two allocations is unknown for both;
  sizes derived from `strlen` or a member are "cannot verify" in the callee
  rather than requirements on callers; a constant size far beyond a buffer is
  "cannot verify", not proven.

### 5b. Identity — Partial

A declaration that shadows another visible where it is declared (an enclosing
block's variable, a parameter, a structured binding, a global or capture the
function names) has its own state, and a local declaration starts its variable
fresh (`1761_shadow_identity_*`). Lambda bodies are analyzed on their own
(`1762_lambda_body_*`): a non-mutable lambda's by-copy captures start with the
constants known where it was written about what they hold themselves (not what
a pointer in them points to), its init captures with their values there (no
resource they hold becomes the body's obligation); by-reference captures start
unknown. A copy that may differ from its original when the body runs starts
unknown: a type with `mutable` members or its own copy constructor, a class
the closure may be moved from once it escapes, a pointer whose nullness a call
sets (`fclose`). A call to a lambda, a constructor or call it is handed to, and
after a lambda escapes (stored, passed, captured) every later call or
non-trivial constructor and the end of a scope holding an object with a
destructor, make unknown what it captures by reference and what its copies
point to, unless the body only reads them (a const or contract-free method
counts as a read); a lambda body inherits what any lambda of its enclosing
functions that escapes may write, wherever that lambda is written. Loop
analyses count a call that may run a lambda as writing what it captures
(`1763_lambda_capture_*`). The body's specific effects are not replayed
(`215_lambda_call_xpass.cpp`).
Source contracts and per-function analysis belong to one function, identified
by qualified name and type; `.dtt` library contracts match free functions in
the global or `std` namespace, or `extern "C"`, by name
(`1764_function_identity_*`).

Not yet covered: generic lambda bodies; `dtt:ensures` on function templates;
a resource an init capture acquires is the closure's, which DTT does not follow
(`[f = fopen(...)]` never leaks); a destructor that runs an escaped lambda when
a jump leaves its scope (only the end of the block is seen); diagnostics name
a shadowing variable by its key (`n'1`);
a captured reference variable (`int &r = x; [&]{ r = 1; }`) writes `x`
(references are step 4); an unannotated method call invalidating its
by-reference arguments (step 4); a container reached by dereferencing a pointer
or smart pointer (`(*p)[i]`, `sp->at(i)`) is not tracked
(`1760_value_model_gaps_xfail.cpp`, step 4).

### 6. Interprocedural Gaps — Historical Closure Claims

Historical closure claim (verify each intended diagnostic):
postcondition to caller (202), output parameter (203), return value props (208),
output param state (211), perfect forwarding (212), lambda invocation (215).

### 7. Constraint Propagation Gaps — Historical Closure Claims

Historical closure claim (verify each intended diagnostic):
OR constraints (201), multi-arg relations (204), arithmetic exprs (205),
void over-invalidation (206), non-zero constraints (492).

---

## P2: Medium Priority

### 8. Loop Analysis — Historical Closure Claims

Historical closure claim (verify each intended diagnostic):
loop invariant (1200), parameterized concept (1201), loop bound gaps (712).

A solver counterexample is "cannot verify", not a proven violation
(`1750_solver_soundness_*`); a bound that no value the facts allow satisfies is
a proven violation, in a loop body as elsewhere (319, 328). DTT cannot yet prove
that a loop reaches every value in its range, so off-by-one accesses in loops
(e.g. 469, 475, 477) are "cannot verify" until loop analysis can show it.

**Plan:** See [ANALYSIS_TECHNIQUES_CATALOG.md](plans/ANALYSIS_TECHNIQUES_CATALOG.md)

### 9. Standard Library — Partial Coverage

Historical closure claims (verify individual scenarios):
string (803), optional (805), unique_ptr (806), shared_ptr (809), weak_ptr (835),
array (815), span (817), deque (819), list (821), stack/queue (827),
string_view (829), variant (831), any (833).

**Remaining:**

| Test | Type |
|------|------|
| `837_std_fstream_xfail.cpp` | File streams (operator<< and operator>>) |

### 10. C Library

> **Status:** Resource leak detection complete (v34). See [C_LIBRARY_SUPPORT.md](C_LIBRARY_SUPPORT.md).

**Completed:**
- ✅ FILE* leak detection (fopen/fclose, popen/pclose, tmpfile, fdopen)
- ✅ Memory leak detection (malloc/free, calloc, realloc, strdup)
- ✅ File descriptor tracking (open/close, socket, accept, dup, mkstemp, epoll_create)
- ✅ Directory streams (opendir/closedir) + DIR* null checking
- ✅ Dynamic libraries (dlopen/dlclose)
- ✅ Memory mapping (mmap/munmap)
- ✅ Bounds checking (memcpy, strcpy, snprintf, etc.)
- ✅ Generic use-after-free: all pointer args checked at all call sites
- ✅ Pthread lifecycle (mutex, cond, rwlock init/destroy)

**Recently closed:** scanf bounds (843), reinterpret_cast (849).

**Remaining gaps:**

| Test | Function |
|------|----------|
| `1501_strtok_cross_call_xfail.cpp` | strtok cross-call state |
| `1502_realpath_conditional_alloc_xfail.cpp` | realpath conditional allocation |
| `1503_getenv_no_free_xfail.cpp` | getenv no-free constraint |
| `1504_readdir_internal_ptr_xfail.cpp` | readdir internal pointer lifetime |

### 11. Buffer Analysis — Partial Coverage

Historical closure claims (verify individual scenarios):
size overflow (851), flexible arrays (852), general tracking (852),
integer overflow (703).

**Verified:** direct `std::vector` initializer-list construction records the
list's element count (`993_initializer_list_size_pass.cpp`). Isolated regressions
in `examples/review/list_construction.cpp` cover empty lists, bounds violations,
explicit allocators, numeric-count overloads and copy/move propagation with
libstdc++ and libc++. Named runtime initializer-list arguments and other container
constructors are outside this fix. `assign({...})` remains a separate path.
Moving a vector directly into a by-value call argument now invalidates the source's
size (`993b_vector_move_argument_fail.cpp`). Bare casts, copies, reference binding
and unevaluated expressions do not count as moves.

**Remaining:**

| Test | Gap |
|------|-----|
| `869_string_concat_tracked_char_xfail.cpp` | String concat |

### 11b. Win32 Gaps

| Test | Gap |
|------|-----|
| `1320_win_variant_xfail.cpp` | Win32 VARIANT type tracking |
| `1340_win_registry_xfail.cpp` | Win32 registry handle tracking |

### 12. Heartbleed Detection (Remaining)

> **Status:** Changes 1-3 complete. See [HEARTBLEED_AUTOTRACK_SPEC.md](archive/HEARTBLEED_AUTOTRACK_SPEC.md).

**Completed:**
- ✅ Source-side unverified bounds (Change 1)
- ✅ Remove constant-only gating (Change 2)
- ✅ Symbolic `_size_bytes` tracking (Change 3)
- ✅ Backward precondition inference (complementary)

**Remaining gaps:**

#### 12a. Allocator Registry (Change 4)

**Gap:** Wrapper allocators (`OPENSSL_malloc`, `WebPSafeMalloc`, `g_malloc`, etc.)
return memory whose `_size_bytes` is unknown to DTT. Copy checks degrade to Category D.

```c
void* buf = OPENSSL_malloc(len);  // DTT: _size_bytes unknown
memcpy(buf, src, len);            // Category D (cannot prove bounds)
```

**Solution:** Recognize allocator wrappers via:
- Built-in list of common wrappers
- Annotation: `[[clang::annotate("dtt:alloc_size_arg=1")]]`
- Plugin arg: `--dtt-alloc-registry=config.json`

**Effort:** ~150-200 lines

#### 12b. Taint Tracking (Change 5)

**Gap:** DTT cannot distinguish untrusted external data from internal values.
Both produce the same Category D warning, but tainted values are far more dangerous.

```c
// Internal (lower risk)
void internal_copy(size_t len) {
    char* buf = malloc(len);
    memcpy(buf, "data", len);  // Category D — same warning as below

// External (Heartbleed-class)
void heartbleed(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);  // len is tainted
    char* buf = malloc(len);
    memcpy(buf, record, len);        // Category D — should be higher severity
}
```

**Solution:**
- Add taint bit to `SymValue` or parallel `TaintEnv`
- Define taint sources: `recv`, `read`, `SSL_read`, `fread`, `scanf`, etc.
- Taint propagates through arithmetic/assignments
- Cleared only when a guard proves `len <= capacity`
- Tainted unproven bounds → stronger warning/error

**Effort:** ~200-300 lines. See [TAINT_TRACKING_PROPOSAL.md](plans/TAINT_TRACKING_PROPOSAL.md).

### 13. Variadic Templates & Fold Expressions — CLOSED

All gaps closed. Pack forwarding (1208) renamed from `_xfail` to `_fail`.
Fold push_back on local vectors (1210) works. Pack parameter accesses (1209)
now produce Category D warnings.

### 15. Uninitialized Memory (Remaining Tiers)

> **Status:** Tier 1 + Tier 2.1 complete (v35). See [UNINITIALIZED_MEMORY_PLAN.md](archive/UNINITIALIZED_MEMORY_PLAN.md).

**Completed:**
- ✅ Scalars, expressions, function args, return values, compound assignment (Tier 1)
- ✅ POD struct member tracking: individual, nested, partial init (Tier 2.1)
- ✅ Variadic: C varargs, parameter packs, fold expressions

**Remaining gaps:**

| Test | Gap | Priority |
|------|-----|----------|
| `716_uninit_struct_array_xfail.cpp` | Struct array element init | P2 |
| `727_stack_aliasing_xfail.cpp` | Stack aliasing (`*p = 1` initializes aliased `x`) | P3 |
| `728_reference_binding_xfail.cpp` | Reference binding (read through ref to uninit) | P2 |
| `729_struct_member_alias_xfail.cpp` | Struct member aliasing | P3 |
| `730_union_member_alias_xfail.cpp` | Union member aliasing | P3 |

### 16. Phase 3 Analysis — Partial

Reported closed: bound expr args (944), complex offset (950).

**Remaining:**

| Test | Gap |
|------|-----|
| `942_phase3_origin_branch_leak_xfail.cpp` | Branch origin |
| `949_phase3_multi_offset_xfail.cpp` | Multi-offset |

---

## P3: Future

### Concurrency
- `845_mutex_xfail.cpp` - Mutex tracking
- `847_future_xfail.cpp` - Future tracking
- `1400_pthread_lifecycle_xfail.cpp` - Pthread lifecycle (cross-function)
- `1505_pthread_lock_state_xfail.cpp` - Pthread lock/unlock state tracking

### Advanced
- `969_capability_xfail.cpp` - Capability tracking
- `980_dimensional_xfail.cpp` - Dimensional analysis

Static params (708) closed (renamed from `_xfail` to `_fail`).

---

## Annotation Format Gaps (from real-world library analysis)

Remaining `.dtt` expressibility gaps discovered by annotating libwebrtc, libusb,
libssh2, s2n-tls, libarchive, libpng, zlib, libdbus, FFmpeg, libuv.
Gaps 1, 5, 7, 8 have been FIXED (`when` guards, named enums, arg bounds, scope-exit on args).

### Cross-Object Constraints (P2)

Ghost fields cannot reference other tracked objects. Needed for parent-child
resource hierarchies (libssh2 session/channel, libuv loop/handle, s2n-tls config/connection).

**Proposed:** `ensures result.@parent_session = arg0` + `requires arg0.@parent_session.@state != 0`

### Conditional Preconditions (P2)

State-dependent rules like "only private connections can be closed" (libdbus).

**Proposed:** `requires_if arg0.@conn_type == 2 then arg0.@conn_state == 2`

### Indirect Out-Parameters (P2)

C APIs returning objects via pointer-to-pointer (libusb `libusb_open(&handle)`,
SQLite `sqlite3_prepare_v2`, FFmpeg `avformat_open_input`).

**Proposed:** `ensures *arg1.@dev_open = 1`

### Ownership Transfer / Consumption (P2)

Functions that invalidate an argument (OpenSSL `SSL_free`, FFmpeg `av_packet_move_ref`).

**Workaround:** `ensures arg0.@alive = 0` via ghost fields. A dedicated `consumes arg0`
keyword would be cleaner.

### Thread Affinity (P3)

Runtime thread identity cannot be verified statically. Defer to TSan/`RTC_DCHECK_RUN_ON`.

---

## Implementation Order

| Sprint | Gap | Priority | Status |
|--------|-----|----------|--------|
| 1 | Union discriminant | P0 | Open (1000, 1002, 1004) |
| 2 | Switch case proofs | P1 | Closed (832g, 1754) |
| ~~3~~ | ~~Reference invalidation~~ | ~~P1~~ | Out of scope (_silent) |
| 4 | Cross-function unions | P1 | Open (1002, 1004) |
| 5 | Loop widening | P2 | Historical closure claim; verify the required loop form |
| Backlog | Remaining xfail gaps | P2-P3 | Query CTest / current fixtures |
| Future | Concurrency | P3 | Unsupported |

---

## Validation

Do not infer coverage percentages from filename counts. Use isolated diagnostic
expectations for implemented cases and retain explicit fixtures for known gaps.
See [Implementation review status](plans/IMPLEMENTATION_REVIEW_STATUS.md) for
verified repairs and current limitations.

---

## Related Docs

- [DTT_SAFETY_ROADMAP.md](DTT_SAFETY_ROADMAP.md) - Future enhancements and C++ safety profile analysis
- [C_LIBRARY_SUPPORT.md](C_LIBRARY_SUPPORT.md) - C library bounds checking and resource tracking
- [VALUE_CONCEPTS_SPEC.md](VALUE_CONCEPTS_SPEC.md) - forall/exists bounded quantifiers
- [WINDOWS_FUNCTION_ANNOTATIONS.md](WINDOWS_FUNCTION_ANNOTATIONS.md) - Windows API annotations
- [plans/ANALYSIS_TECHNIQUES_CATALOG.md](plans/ANALYSIS_TECHNIQUES_CATALOG.md) - Analysis techniques catalog (loop, path, scalability)

### Historical Scan Sources for xpass Discovery

These are previously recorded scan results, not revalidated measurements from this repair pass.

- **libwebp** (https://github.com/webmproject/libwebp) — 946 findings across 53 files, all FPs
- **giflib** (https://github.com/mirrorer/giflib) — 102 findings across 5 lib files, all FPs (one latent bug: missing `fdopen` NULL check in `egif_lib.c:64`)


## Negative-fixture audit

Regular `_fail` fixtures assert intended diagnostic locations. A fixture can no
longer pass solely because some other statement produces a DTT error. Scenarios
that remain unsupported inside mixed fixtures are labeled `KNOWN GAP`; those
comments do not count as checked detections.

The audit reclassified safe fixtures that had passed only through false positives:

- Interprocedural state and return tracking: `204_multiarg_prop_xpass.cpp`,
  `215_lambda_call_xpass.cpp` (`202` and `208` pass since relational `ensures`
  are parsed).
- Guard handling: `330_ternary_guard_xpass.cpp`,
  `358_guard_with_member_call_bool_xpass.cpp`, `712_loop_bound_gaps_xpass.cpp`.
- Library/layout tracking: `806_std_unique_ptr_xpass.cpp`, `852_flex_array_xpass.cpp`.
- Loop and template inference: `1019_auto_postcond_loop_xpass.cpp`,
  `1200_loop_invariant_xpass.cpp`, `1208_pack_forwarding_xpass.cpp`.

The drain-to-empty case (`1019_auto_postcond_loop_fail.cpp`) is "cannot
verify": the loop's write leaves the field unknown, but its final value is not
inferred. Array-element initialization remains a gap
in `713_array_element_uninit_xfail.cpp`, with unrelated index-bound errors removed.
These are existing analyzer limitations exposed by correcting the test oracle.

External-library `_fail` fixtures use the same location-aware runner. Acquisition
results are guarded before testing later lifecycle misuse, and required headers
are included so syntax errors cannot satisfy a negative test. Existing missed
cases previously hidden by incidental diagnostics are recorded as
`libuv_handle_after_close_xfail.cpp`, `libuv_timer_xfail.cpp`,
`libuv_tcp_xfail.cpp`, and `s2n_cve_ghsa_mm47_xfail.cpp`.
