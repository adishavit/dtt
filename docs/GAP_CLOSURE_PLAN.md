# DTT Gap Closure Plan

Consolidated view of all known gaps and implementation plans.

---

## Summary

| Category | Count | Priority | Status |
|----------|-------|----------|--------|
| **Union/Sum Types** | 9 | P0-P1 | Partial |
| **Control Flow** | 8 | P1 | Planned |
| **Interprocedural** | 6 | P1 | Partial |
| **Loops & Arrays** | 5 | P1-P2 | Planned |
| **Standard Library** | 18 | P2 | Planned |
| **C Library** | 5 | P2 | **Mostly Complete** |
| **Concurrency** | 2 | P3 | Future |
| **Variadic/Fold** | 2 | P2 | Documented |
| **Uninitialized Memory** | 7 | P2-P3 | Partial |
| **Advanced** | 9 | P2-P3 | Future |
| **False Positives (xpass)** | 9 | FP1-FP3 | Partial |

**Total: 62 xfail tests, 9 xpass classes** | **Priority:** P0=Critical, P1=High, P2=Medium, P3=Future; FP1=High-impact, FP2=Medium, FP3=Low

---

## Recently Closed

| Gap | Test | Branch |
|-----|------|--------|
| **Auto resource leak detection** | `1012_auto_resource_leak_fail.cpp` | main |
| **Resource-centric tracking (v34)** | `1013_auto_resource_pass.cpp` | main |
| **C library annotations (40+ functions)** | `988_fopen_patterns_pass.cpp` | main |
| Auto-postcondition: arithmetic | `1014_auto_postcond_arithmetic_fail.cpp` | main |
| Auto-postcondition: nested fields | `1027_auto_postcond_nested_complex_xfail.cpp` | main |
| Auto-postcondition: param names | `1027_auto_postcond_nested_complex_xfail.cpp` | main |
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
| External library test suite (218 tests) | `tests/external/` | main (PR #36) |
| `.dtt` frame semantics (hasDTTAnnotations) | `1102_dtt_frame_semantics_pass.cpp` | fix/dtt-frame-semantics |
| Deprecated bare `dtt:field=value` syntax removed | `01_basic_fail.cpp`, `02_basic_pass.cpp` | main |
| Test initialization: struct members properly initialized | 53 test files | main |
| Memory functions (memset/memcpy/memmove) as initializers | `726_memory_functions_pass.cpp` | main |
| freopen null proof invalidation | `840_fopen_freopen_fail.cpp` | main |
| Bitfield width bounds (FP6) | `1705_bitfield_width_pass.cpp` | main |
| Out-parameter initialization (FP3) | `1702_out_param_init_pass.cpp` | main |

---

## False Positives (xpass Tests)

Discovered by scanning real-world C codebases (libwebp, giflib) with DTT. These are
safe code patterns that DTT incorrectly rejects. Each class documents a category of
false positive and the `_xpass` test that captures it.

**xpass semantics:** Mirror of xfail. Uses `WILL_FAIL TRUE` in CMake. When FP exists,
DTT fires → compilation fails → test PASSES. When FP is fixed, DTT stays silent →
compilation succeeds → test FAILS → rename to `_pass.cpp`.

### Summary

| # | FP Class | Test | Priority | Impact | Source |
|---|----------|------|----------|--------|--------|
| 1 | sizeof unevaluated | `1701_sizeof_unevaluated_xpass.cpp` | FP1 | Medium | libwebp |
| 2 | Inline asm output operands | `1700_asm_output_operands_xpass.cpp` | FP2 | Low | libwebp |
| 3 | Out-parameter initialization | `1702_out_param_init_pass.cpp` | FP1 | High | libwebp, giflib | **CLOSED** |
| 4 | Ownership transfer via return | `1703_ownership_transfer_return_xpass.cpp` | FP1 | High | libwebp, giflib |
| 5 | Unchecked pointer parameters | `1704_unchecked_param_xpass.cpp` | FP3 | Very High | libwebp, giflib |
| 6 | Bitfield width constraints | `1705_bitfield_width_pass.cpp` | FP2 | Low | libwebp | **CLOSED** |
| 7 | Modular/wrapping invariants | `1706_modular_invariant_xpass.cpp` | FP2 | Low | libwebp, giflib |
| 8 | Cross-function bounds validation | `1707_cross_func_bounds_xpass.cpp` | FP1 | High | libwebp |
| 9 | Negative signed index | `1708_negative_signed_index_xpass.cpp` | FP2 | Low | libwebp |

### FP1: sizeof Unevaluated Operands

**Test:** `1701_sizeof_unevaluated_xpass.cpp`

**Gap:** DTT flags `sizeof(*ptr)` as a null/uninitialized pointer dereference, but
`sizeof` never evaluates its operand — it resolves at compile time.

```c
// libwebp src/enc/webp_enc.c:157
VP8Encoder* enc;  // uninitialized
size_t preds_size = 4 * mb_w * sizeof(*enc->preds_);  // DTT flags this

// libwebp src/enc/token_enc.c:73
VP8Tokens* page = NULL;
size_t size = sizeof(*page) + tokens_count * sizeof(token_t);  // DTT flags this
```

**Solution:** In the `MemberExpr` / `UnaryOperator` visitors, check whether the
expression is inside a `sizeof`, `alignof`, `decltype`, or `noexcept` unevaluated
context. Skip property tracking for unevaluated operands.

**Effort:** ~50-80 lines

### FP2: Inline Asm Output Operands

**Test:** `1700_asm_output_operands_xpass.cpp`

**Gap:** GCC-style inline asm with `"=r"(var)` output constraints initializes variables,
but DTT doesn't model asm semantics and flags the variable as uninitialized.

```c
// libwebp src/dsp/cpu.c:74 (xgetbv pattern)
uint32_t eax, edx;
__asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
return ((uint64_t)edx << 32) | eax;  // DTT flags eax, edx as uninitialized
```

**Solution:** In `VisitGCCAsmStmt`, parse output constraints (prefixed with `=` or `+`)
and mark the corresponding variables as initialized.

**Effort:** ~60-100 lines

### FP3: Out-Parameter Initialization — CLOSED

**Test:** `1702_out_param_init_pass.cpp` (was `_xpass`)

**Fix:** Out-parameter initialization heuristic in `VisitCallExpr`: when `&local_var` is
passed to a non-const `T*` parameter, mark the local as initialized. Covers scalars and
structs. The heuristic assumes the callee writes through the pointer (standard C/C++
out-parameter convention).

### FP4: Ownership Transfer via Return

**Test:** `1703_ownership_transfer_return_xpass.cpp`

**Gap:** Allocator wrapper functions (`WebPSafeMalloc`, `WebPSafeCalloc`, custom
`malloc`+`init` wrappers) return allocated memory. DTT tracks the allocation inside the
wrapper but cannot see that ownership transfers to the caller via return value.

```c
// libwebp src/utils/utils.c:210
void* WebPSafeMalloc(uint64_t nmemb, size_t size) {
    void* ptr = malloc((size_t)total_size);  // DTT tracks allocation here
    return ptr;  // ownership transfers to caller — DTT reports leak
}

// giflib lib/egif_lib.c
GifFileType* EGifOpenFileName(...) {
    GifFileType* GifFile = (GifFileType*)malloc(sizeof(GifFileType));
    ...
    return GifFile;  // ownership transfer
}
```

**Solution:** Recognize that `return ptr` where `ptr` holds a tracked resource transfers
ownership to the caller. Clear the leak obligation on returned resources.

**Effort:** ~80-120 lines

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

### FP7: Modular/Wrapping Invariants

**Test:** `1706_modular_invariant_xpass.cpp`

**Gap:** DTT doesn't model modular arithmetic invariants. Index variables maintained
modulo a constant are always in-bounds, but DTT can't prove it.

```c
// libwebp src/utils/random_utils.h:47
typedef struct { int index1, index2; int tab[55]; } VP8LRandom;
// index1 = index1 % 55; index2 = index2 % 55;
int v = random->tab[random->index1];  // always in-bounds, DTT flags it

// giflib lib/gif_hash.c:73
#define HT_SIZE 8192
#define HT_KEY_MASK 0x1FFF  // = 8191
HKey = Key & HT_KEY_MASK;
HTable[HKey] = ...;  // always < 8192, DTT flags it
```

**Solution:** Recognize `x % N` and `x & MASK` as producing bounded results. Add
implicit proof `0 <= result < N` after modulo, or `0 <= result <= MASK` after bitmask.

**Effort:** ~80-120 lines

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

### FP9: Negative Signed Index

**Test:** `1708_negative_signed_index_xpass.cpp`

**Gap:** DTT flags signed integer array indices as potentially negative, even when
the value is guarded to be non-negative by an upper-bound check.

```c
// libwebp src/dsp/lossless_common.h:149
static int DistanceToPlaneCode(int distance) {
    // Only upper bound checked: distance < DISTANCE_TABLE_SIZE
    return kDistanceTable[distance];  // DTT: distance could be negative
}
```

**Solution:** When checking `x < N` for array access, also verify `x >= 0` for signed
types. Better: recognize common patterns where function context implies non-negative
(e.g., distance values, sizes, counts).

**Effort:** ~40-60 lines

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

### 2. Switch Case Proofs (variant)

**Test:** `832g_variant_switch_xfail.cpp`

**Gap:** `switch(v.index()) { case 0: std::get<0>(v); }` has no proof in case blocks.

**Solution:** In `VisitSwitchStmt`, detect `switch(v.index())` and add proof per case.

**Effort:** 100-150 lines, 1 week

### 3. Reference Invalidation

**Test:** `832d_variant_ref_invalidation_silent.cpp`

**Gap:** Dangling references after variant reassignment not detected.

```cpp
auto& vec = std::get<vector>(v);
v = 42;           // vec now dangling
vec.push_back(4); // USE-AFTER-FREE not caught
```

**Solution:** Track `ref._depends_on`, invalidate on reassignment.

**Effort:** 200-250 lines, 2 weeks

### 4. Cross-Function Union Tracking

**Tests:** `1002_union_cve_kernel_xfail.cpp`, `1004_union_cve_parser_xfail.cpp`

**Gap:** Union state lost when passed to function.

**Solution:** Parameter annotation `[[dtt::union_active("member")]]`.

**Effort:** 150-200 lines, 1-2 weeks

### 4b. Union Type Punning Detection

**Gap:** Pointer/integer confusion through unions (general case). Specific CVE patterns
are already caught (`1001_union_cve_ptr_int_fail`, `1005_union_cve_size_fail`), but
subtler type punning via reinterpretation (e.g., `union { void* p; uintptr_t n; }`)
is not detected when the confusion doesn't match a known CVE shape.

**Priority:** P2

**Effort:** ~100-150 lines

### 5. Control Flow Gaps

| Test | Gap |
|------|-----|
| `140_nullptr_ternary_xfail.cpp` | Ternary expressions |
| `330_ternary_guard_xfail.cpp` | Ternary guards |
| `329_range_for_guard_xfail.cpp` | Range-for guards |
| `358_guard_with_member_call_bool_xfail.cpp` | Member call guards |
| `432_buffer_ternary_xfail.cpp` | Buffer in ternary |
| `135_nullptr_aliasing_xfail.cpp` | Pointer aliasing |
| `144_nullptr_complex_sources_xfail.cpp` | Complex null sources |
| `145_nullptr_indirect_init_xfail.cpp` | Indirect init |

### 6. Interprocedural Gaps

| Test | Gap |
|------|-----|
| `202_xfail_interprocedural.cpp` | Postcondition to caller |
| `203_xfail_output_param.cpp` | Output parameter |
| `208_xfail_return_tracking.cpp` | Return value props |
| `211_xfail_output_param_tracking.cpp` | Output param state |
| `212_xfail_forwarding_ref.cpp` | Perfect forwarding |
| `215_xfail_lambda_call.cpp` | Lambda invocation |

### 7. Constraint Propagation Gaps

| Test | Gap |
|------|-----|
| `201_xfail_or_constraint_prop.cpp` | OR constraints |
| `204_xfail_multiarg_prop.cpp` | Multi-arg relations |
| `205_xfail_expr_constraint_prop.cpp` | Arithmetic exprs |
| `206_xfail_void_heuristic.cpp` | Void over-invalidation |
| `492_nonzero_constraint_xfail.cpp` | Non-zero constraints |

---

## P2: Medium Priority

### 8. Loop Analysis

**Tests:** `1200_loop_invariant_xfail.cpp`, `1201_parameterized_concept_xfail.cpp`, `712_loop_bound_gaps_xfail.cpp`

**Gap:** No loop invariant inference or widening.

**Plan:** See [LOOP_PATH_ANALYSIS_PLAN.md](plans/LOOP_PATH_ANALYSIS_PLAN.md)

### 9. Standard Library

| Test | Type |
|------|------|
| `803_std_string_xfail.cpp` | std::string |
| `805_std_optional_xfail.cpp` | std::optional |
| `806_std_unique_ptr_xfail.cpp` | std::unique_ptr |
| `809_std_shared_ptr_xfail.cpp` | std::shared_ptr |
| `835_std_weak_ptr_xfail.cpp` | std::weak_ptr |
| `815_std_array_xfail.cpp` | std::array |
| `817_std_span_xfail.cpp` | std::span |
| `819_std_deque_xfail.cpp` | std::deque |
| `821_std_list_xfail.cpp` | std::list |
| `827_std_stack_queue_xfail.cpp` | std::stack/queue |
| `829_std_string_view_xfail.cpp` | std::string_view |
| `831_std_variant_xfail.cpp` | std::variant (includes moved-from state) |
| `833_std_any_xfail.cpp` | std::any (includes moved-from state) |
| `837_std_fstream_xfail.cpp` | File streams |

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

**Remaining gaps:**

| Test | Function |
|------|----------|
| `843_scanf_xfail.cpp` | scanf bounds |
| `849_nullptr_reinterpret_cast_xfail.cpp` | reinterpret_cast |
| `1501_strtok_cross_call_xfail.cpp` | strtok cross-call state |
| `1502_realpath_conditional_alloc_xfail.cpp` | realpath conditional allocation |
| `1503_getenv_no_free_xfail.cpp` | getenv no-free constraint |
| `1504_readdir_internal_ptr_xfail.cpp` | readdir internal pointer lifetime |

### 11. Buffer Analysis

| Test | Gap |
|------|-----|
| `851_integer_overflow_size_xfail.cpp` | Size overflow |
| `852_flex_array_xfail.cpp` | Flexible arrays |
| `852_tracking_gaps_xfail.cpp` | General tracking |
| `869_string_concat_tracked_char_xfail.cpp` | String concat |
| `703_integer_overflow_size_xfail.cpp` | Integer overflow |
| `993_initializer_list_size_xfail.cpp` | Initializer list |

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

### 13. Variadic Templates & Fold Expressions

| Test | Gap | Status |
|------|-----|--------|
| `1210_fold_push_back_local_pass.cpp` | Fold push_back on local vectors | **CLOSED** |
| `1208_pack_forwarding_xfail.cpp` | Fold push_back on reference params | Open (inter-procedural) |
| `1209_pack_property_silent.cpp` | Pack parameter _size propagation | Improved (warns) |

**Closed:** Fold push_back/emplace_back counting on **local** vectors now works. DTT analyzes
template instantiations (Pass 2b) where fold expressions are fully expanded into individual
method calls. The `_size` property is correctly incremented for each expanded call.

**Remaining (xfail):** When fold push_back operates on a **reference** parameter, the caller
doesn't see the `_size` changes. This is a general inter-procedural limitation.

**Improved (silent→warns):** Pack parameter accesses now produce Category D warnings
(solver-failed) instead of being completely silent, because DTT now visits template
instantiation bodies where the accesses are type-resolved.

**What works:** Uninitialized value tracking through all variadic patterns — C varargs,
parameter packs, recursive variadic templates, fold expressions, if constexpr dispatch,
std::apply/invoke, CTAD, pack lambda captures.

### 15. Uninitialized Memory (Remaining Tiers)

> **Status:** Tier 1 + Tier 2.1 complete (v35). See [UNINITIALIZED_MEMORY_PLAN.md](archive/UNINITIALIZED_MEMORY_PLAN.md).

**Completed:**
- ✅ Scalars, expressions, function args, return values, compound assignment (Tier 1)
- ✅ POD struct member tracking: individual, nested, partial init (Tier 2.1)
- ✅ Variadic: C varargs, parameter packs, fold expressions

**Remaining gaps:**

| Tier | Gap | Overlap | Priority |
|------|-----|---------|----------|
| 2.2 | Array element init (`arr[5]` never written) | — | P2 |
| 2.3 | Reference binding (read through ref to uninit) | — | P2 |
| 3.4 | Designated initializer tracking (`S s = {.a = 1}; use(s.b)`) | — | P2 |
| 4.3 | Conditional output (`tryParse(s, &x)` if returns true) | — | P2 |
| 5.1 | Aliasing (`*p = 1` initializes aliased `x`) | — | P3 |
| 5.2 | Heap objects (`new int` vs `new int()`) | — | P3 |
| 5.3 | Memory function init (`memset(&x, 0, sizeof(x))`) | — | P2 |

**Already tracked elsewhere:**
- Union members (2.4) → Section 1 (Union Discriminant)
- Conditional/all-paths init (3.1-3.2) → Section 5 (Control Flow)
- Loop init (3.3) → Section 8 (Loop Analysis)
- Output parameters (4.1) → Section 6 (Interprocedural) + xpass FP3
- scanf (4.2) → Section 10 C Library (`843_scanf_xfail.cpp`)

### 16. Phase 3 Analysis

| Test | Gap |
|------|-----|
| `942_phase3_origin_branch_leak_xfail.cpp` | Branch origin |
| `944_phase2_bound_expr_arg_missing_xfail.cpp` | Bound expr args |
| `949_phase3_multi_offset_xfail.cpp` | Multi-offset |
| `950_phase3_complex_offset_expr_xfail.cpp` | Complex offset |

---

## P3: Future

### Concurrency
- `845_mutex_xfail.cpp` - Mutex tracking
- `847_future_xfail.cpp` - Future tracking
- `1505_pthread_lock_state_xfail.cpp` - Pthread lock/unlock state tracking

### Advanced
- `708_static_function_params_comprehensive_xfail.cpp` - Static params
- `969_capability_xfail.cpp` - Capability tracking
- `980_dimensional_xfail.cpp` - Dimensional analysis

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

| Sprint | Gap | Priority |
|--------|-----|----------|
| 1 | Union discriminant | P0 |
| 2 | Switch case proofs | P1 |
| 3 | Reference invalidation | P1 |
| 4 | Cross-function unions | P1 |
| 5 | Loop widening | P2 |
| Backlog | Std library | P2 |
| Future | Concurrency | P3 |

---

## Success Metrics

| Type | Current | Target |
|------|---------|--------|
| std::variant | 70% | 95% |
| std::expected | 95% | 98% |
| C/C++ union | 50% | 80% |
| CVE detection | ~30% | ~80% |

---

## Related Docs

- [DTT_SAFETY_ROADMAP.md](DTT_SAFETY_ROADMAP.md) - Future enhancements and C++ safety profile analysis
- [C_LIBRARY_SUPPORT.md](C_LIBRARY_SUPPORT.md) - C library bounds checking and resource tracking
- [VALUE_CONCEPTS_SPEC.md](VALUE_CONCEPTS_SPEC.md) - forall/exists bounded quantifiers
- [WINDOWS_FUNCTION_ANNOTATIONS.md](WINDOWS_FUNCTION_ANNOTATIONS.md) - Windows API annotations
- [plans/LOOP_PATH_ANALYSIS_PLAN.md](plans/LOOP_PATH_ANALYSIS_PLAN.md) - Loop analysis

### Scan Sources for xpass Discovery

- **libwebp** (https://github.com/webmproject/libwebp) — 946 findings across 53 files, all FPs
- **giflib** (https://github.com/mirrorer/giflib) — 102 findings across 5 lib files, all FPs (one latent bug: missing `fdopen` NULL check in `egif_lib.c:64`)
