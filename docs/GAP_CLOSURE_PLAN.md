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
| **C Library** | 5 | P2 | Partial |
| **Concurrency** | 2 | P3 | Future |
| **Advanced** | 9 | P2-P3 | Future |

**Total: 62 xfail tests** | **Priority:** P0=Critical, P1=High, P2=Medium, P3=Future

---

## Recently Closed

| Gap | Test | Branch |
|-----|------|--------|
| Transitive preconditions | `950_transitive_precond_pass.cpp` | dual-mechanisms |
| Counterexample generation | `951_refutation_pass.cpp` | dual-mechanisms |
| Frame inference | `952_frame_inference_pass.cpp` | dual-mechanisms |
| std::expected monadic | `999_expected_monadic_pass.cpp` | main |
| Value Concepts | `983_symbolic_bounds_pass.cpp` | main |

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
| `831_std_variant_xfail.cpp` | std::variant |
| `833_std_any_xfail.cpp` | std::any |
| `837_std_fstream_xfail.cpp` | File streams |

### 10. C Library

| Test | Function |
|------|----------|
| `813_strcpy_xfail.cpp` | strcpy bounds |
| `839_malloc_xfail.cpp` | malloc tracking |
| `841_fopen_xfail.cpp` | FILE* tracking |
| `843_scanf_xfail.cpp` | scanf bounds |
| `849_nullptr_reinterpret_cast_xfail.cpp` | reinterpret_cast |

### 11. Buffer Analysis

| Test | Gap |
|------|-----|
| `850_buffer_overread_xfail.cpp` | Over-read |
| `851_integer_overflow_size_xfail.cpp` | Size overflow |
| `852_flex_array_xfail.cpp` | Flexible arrays |
| `852_tracking_gaps_xfail.cpp` | General tracking |
| `869_string_concat_tracked_char_xfail.cpp` | String concat |
| `703_integer_overflow_size_xfail.cpp` | Integer overflow |
| `993_initializer_list_size_xfail.cpp` | Initializer list |

### 12. Phase 3 Analysis

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

### Advanced
- `708_static_function_params_comprehensive_xfail.cpp` - Static params
- `969_capability_xfail.cpp` - Capability tracking
- `980_dimensional_xfail.cpp` - Dimensional analysis

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

- [SUM_TYPE_TRACKING.md](SUM_TYPE_TRACKING.md) - Variant/any/expected/union
- [VALUE_CONCEPTS_SPEC.md](VALUE_CONCEPTS_SPEC.md) - forall/exists
- [plans/LOOP_PATH_ANALYSIS_PLAN.md](plans/LOOP_PATH_ANALYSIS_PLAN.md) - Loop analysis
