# Gap Closure Plan

This document prioritizes remaining work to close gaps in DTT's type tracking.

---

## Priority Levels

| Priority | Meaning | Timeline |
|----------|---------|----------|
| **P0** | Critical - blocks CVE detection | Next sprint |
| **P1** | High - significant safety gap | 2-3 sprints |
| **P2** | Medium - improves coverage | Backlog |
| **P3** | Low - nice to have | Future |

---

## P0: Critical Gaps

### 1. Union Discriminant Correlation

**Gap:** Cannot correlate external tag field with union member access.

**Impact:** Blocks detection of CVE-2022-41033, COMFUSION patterns, and most real-world tagged union vulnerabilities.

**Current behavior:**
```cpp
struct TaggedUnion {
    int tag;
    union { int i; float f; } data;
};

void process(TaggedUnion *t) {
    if (t->tag == TAG_INT) {
        int x = t->data.i;  // DTT cannot prove this is safe
    }
}
```

**Solution approach:**
1. Detect struct containing both enum/int field and union
2. Track `struct._tag_field` → `struct.union._union_active` correlation
3. Extract proofs from `if (s.tag == CONSTANT)` patterns
4. Map enum constants to union member names (heuristic or annotation)

**Estimated effort:** 300-400 lines, 2-3 weeks

**Test files:** `1000_union_cve_discriminant_xfail.cpp`

---

### 2. std::expected Monadic Operations ✅ CLOSED

**Status:** Implemented in DTTPlugin.cpp

**Solution:**
- `and_then(F)`: if input has error, output has error; if input has value, output has value
- `transform(F)`: output._has_value = input._has_value
- `or_else(F)`: if input has value, output has value; if error, conservative (guard needed)
- `transform_error(F)`: output._has_value = input._has_value

**Test files:** `999_expected_monadic_pass.cpp`, `999_expected_monadic_fail.cpp`

---

## P1: High Priority Gaps

### 3. Switch Case Proof Extraction (variant)

**Gap:** `switch(v.index()) { case 0: std::get<0>(v); }` - no proof in case blocks.

**Impact:** Idiomatic variant access pattern rejected.

**Solution approach:**
1. In `VisitSwitchStmt`, detect `switch(v.index())`
2. For each case, add proof `v._variant_index == case_value`
3. Handle `default:` case (unknown index)

**Estimated effort:** 100-150 lines, 1 week

**Test files:** `832g_variant_switch_xfail.cpp`

---

### 4. Reference Invalidation Detection (variant/any)

**Gap:** Dangling references after variant/any reassignment not detected.

**Impact:** Use-after-free from reference invalidation.

**Current behavior:**
```cpp
std::variant<int, std::vector<int>> v = std::vector<int>{1,2,3};
auto& vec = std::get<std::vector<int>>(v);
v = 42;  // vec is now dangling
vec.push_back(4);  // USE-AFTER-FREE - not caught
```

**Solution approach:**
1. Track `ref._depends_on = "variant_path"` when reference extracted
2. On variant reassignment, scan for dependent references
3. Mark dependent references as `_invalidated = 1`
4. Check `_invalidated != 1` before reference use

**Estimated effort:** 200-250 lines, 2 weeks

**Test files:** `832d_variant_ref_invalidation_silent.cpp`

---

### 5. Cross-Function Union Tracking

**Gap:** Union state lost when passed as pointer to function.

**Impact:** Most CVE patterns involve unions passed to functions.

**Current behavior:**
```cpp
void process(union U* u) {
    // u._union_active is UNKNOWN
    int x = u->i;  // Cannot validate
}
```

**Solution approach:**
1. Allow function parameter annotations: `[[dtt::union_active("member")]]`
2. Propagate tracking through pointer parameters when annotations present
3. Conservative: allow access when unknown (current behavior)

**Estimated effort:** 150-200 lines, 1-2 weeks

**Test files:** `1002_union_cve_kernel_xfail.cpp`, `1004_union_cve_parser_xfail.cpp`

---

## P2: Medium Priority Gaps

### 6. Type Punning Detection (union)

**Gap:** Writing int, reading as pointer not detected as type confusion.

**Solution:** Track type category (integral vs pointer vs float) in `_union_active_category` and warn on category mismatch.

**Estimated effort:** 100 lines, 1 week

---

### 7. Moved-from State Tracking (any/variant)

**Gap:** Moved-from variant/any state not tracked.

**Solution:** Set `_moved_from = 1` after `std::move()`, check before access.

**Estimated effort:** 50-100 lines, 1 week

---

### 8. std::expected `error_or()` Documentation

**Gap:** `error_or(default)` not documented as always-safe.

**Solution:** Add to DTTStdLib.cpp annotations, no precondition needed.

**Estimated effort:** 10 lines, trivial

---

## P3: Low Priority / Future

### 9. User-Defined Sum Type Annotations

**Gap:** Users cannot annotate their own `Result<T,E>` types.

**Solution:** Extend annotation syntax for sum type support (see Architecture section in VARIANT_ANY_TRACKING_PLAN.md).

**Estimated effort:** 500+ lines, significant

---

### 10. Variant Modification Inside Visitor

**Gap:** Modifying variant during `std::visit` not warned.

**Solution:** Track if variant is being visited, warn on assignment during visit.

**Estimated effort:** 50-100 lines, low priority

---

## Implementation Order

| Sprint | Gap | Priority | Status |
|--------|-----|----------|--------|
| ✅ | Monadic operations (expected) | P0 | DONE |
| 1 | Switch case proofs (variant) | P1 | Pending |
| 2 | Discriminant correlation (union) | P0 | Pending |
| 3 | Reference invalidation | P1 | Pending |
| 4 | Cross-function union tracking | P1 | Pending |
| Backlog | Type punning detection | P2 | Pending |
| Backlog | Moved-from tracking | P2 | Pending |
| Future | User-defined sum types | P3 | Pending |

---

## Success Metrics

After closing P0 and P1 gaps:

| Type | Current Coverage | Target Coverage |
|------|------------------|-----------------|
| `std::variant` | 70% | 95% |
| `std::any` | 75% | 90% |
| `std::expected` | 90% ✅ | 95% |
| C/C++ `union` | 50% | 80% |

**CVE Detection Rate:**
- Current: ~30% of CVE patterns in test suite caught
- Target: ~80% of CVE patterns caught

---

## Related Documentation

- [TYPE_TRACKING_STATUS.md](TYPE_TRACKING_STATUS.md) - Current implementation status
- [VARIANT_ANY_TRACKING_PLAN.md](VARIANT_ANY_TRACKING_PLAN.md) - Original design
- [EXPECTED_TRACKING_PLAN.md](EXPECTED_TRACKING_PLAN.md) - std::expected design
- [UNION_TRACKING_PLAN.md](UNION_TRACKING_PLAN.md) - Union tracking design
