# DTT Sum Type Tracking

Comprehensive documentation for tracking `std::variant`, `std::any`, `std::expected`, and C/C++ unions.

---

## Status Overview

| Type | Core Coverage | API Completeness | Key Gaps |
|------|---------------|------------------|----------|
| `std::variant` | 95% | ~70% | Switch proofs, ref invalidation |
| `std::any` | 90% | ~75% | Moved-from state |
| `std::expected` | 95% | ~90% | error_or (minor) |
| C/C++ `union` | 80% | ~50% | Discriminant correlation |

---

## 1. std::variant<Ts...>

### Implemented

| Operation | Property | Notes |
|-----------|----------|-------|
| `get<I>(v)` | `_variant_index == I` | Precondition check |
| `get<T>(v)` | `_variant_index == type_index(T)` | Type-to-index mapping |
| `get_if<I>(&v)` | Returns nullptr if mismatch | Null tracking |
| `get_if<T>(&v)` | Returns nullptr if mismatch | Null tracking |
| `holds_alternative<T>(v)` | Proof extraction | Both branches |
| `v.index()` comparison | Proof extraction | `if (v.index() == N)` |
| `v.index()` return | Returns `_variant_index` | Field return |
| `valueless_by_exception()` | `_has_value == 0` | Rare case |
| `emplace<I>()` | Sets `_variant_index = I` | Template arg extraction |
| `emplace<T>()` | Sets `_variant_index = type_index(T)` | Type mapping |
| `operator=` | Updates `_variant_index` | Type inference |
| `std::visit()` | Always safe | No precondition |
| `std::swap()` | Swaps `_variant_index` | Both variants |
| Copy/move | Propagates `_variant_index` | Assignment tracking |

### Gaps

| Gap | Type | Description | Test |
|-----|------|-------------|------|
| Switch case proofs | xfail | `switch(v.index()) { case 0: get<0>(v); }` | `832g_xfail` |
| Reference invalidation | silent | Dangling refs after reassignment | `832d_silent` |

### Annotations (annotations/std.dtt)

```cpp
// Default constructor: first alternative active
addPostConst("variant", "variant", 0, "_variant_index", 0);

// valueless_by_exception(): true if no value
addBoolPost("variant", "valueless_by_exception", "_has_value", 0, 1);

// index(): returns current index
addReturns("variant", "index", "_variant_index");
```

---

## 2. std::any

### Implemented

| Operation | Property | Notes |
|-----------|----------|-------|
| `any_cast<T>(a)` | `_has_value && _any_type == T` | Type + value check |
| `any_cast<T>(&a)` | Returns nullptr if mismatch | Pointer form |
| `a.has_value()` | Returns `_has_value` | Bool return |
| `a.type() == typeid(T)` | Proof extraction | Both branches |
| `a.reset()` | Sets `_has_value = 0` | Clears any |
| `a.emplace<T>()` | Sets `_has_value = 1`, tracks type | Template arg |
| `operator=` | Updates `_has_value`, tracks type | Type inference |
| `make_any<T>()` | Tracks type T | Factory function |

### Gaps

| Gap | Type | Description | Test |
|-----|------|-------------|------|
| Moved-from state | xfail | Moved-from any not tracked as empty | `833_xfail` |

### Annotations (annotations/std.dtt)

```cpp
addPostConst("any", "any", 0, "_has_value", 0);  // Default: empty
addPostConst("any", "any", 1, "_has_value", 1);  // From value
addBoolPost("any", "has_value", "_has_value", 1, 0);
addPostConst("any", "reset", 0, "_has_value", 0);
addPostConst("any", "operator=", 1, "_has_value", 1);
for (unsigned i = 0; i <= 3; i++)
    addPostConst("any", "emplace", i, "_has_value", 1);
```

---

## 3. std::expected<T, E> (C++23)

### Implemented

| Operation | Property | Notes |
|-----------|----------|-------|
| `e.value()` | `_has_value == 1` | Throws if no value |
| `*e` | `_has_value == 1` | UB if no value |
| `e->member` | `_has_value == 1` | UB if no value |
| `e.error()` | `_has_value == 0` | UB if has value |
| `e.has_value()` | Returns `_has_value` | Bool return |
| `bool(e)` | Returns `_has_value` | Conversion |
| `e.value_or(default)` | Always safe | No precondition |
| `e.emplace()` | Sets `_has_value = 1` | Postcondition |
| Construction from T | Sets `_has_value = 1` | Value path |
| Construction from unexpected | Sets `_has_value = 0` | Error path |
| `e.and_then(F)` | Propagates `_has_value` | If error, stays error |
| `e.transform(F)` | Propagates `_has_value` | Preserves state |
| `e.or_else(F)` | Propagates `_has_value` | If value, stays value |
| `e.transform_error(F)` | Propagates `_has_value` | Preserves state |

### Remaining Gaps

| Gap | Description | Priority |
|-----|-------------|----------|
| `error_or(default)` | Always safe, not documented | P3 |

### Annotations (annotations/std.dtt)

```cpp
addPostConst("expected", "expected", 0, "_has_value", 1);  // Default: has value
addPostConst("expected", "expected", 1, "_has_value", 1);  // From value
addPre("expected", "value", 0, hasValue());
addPre("expected", "operator*", 0, hasValue());
addPre("expected", "operator->", 0, hasValue());
addPre("expected", "error", 0, noValue());
addBoolPost("expected", "has_value", "_has_value", 1, 0);
addBoolPost("expected", "operator bool", "_has_value", 1, 0);
for (unsigned i = 0; i <= 3; i++)
    addPostConst("expected", "emplace", i, "_has_value", 1);
```

---

## 4. C/C++ Union

### Implemented

| Operation | Property | Notes |
|-----------|----------|-------|
| Member write | Sets `_union_active = "member"` | Activates member |
| Member read | Checks `_union_active == "member"` | Validates access |
| Brace init | `_union_active` = first member | Default behavior |
| Designated init | `_union_active` = designated member | `{.f = 3.14}` |
| Address-of | Always allowed | `&u.member` |
| char/byte access | Always allowed | Serialization |
| Common initial sequence | Allowed for matching prefix | C++ rule |

### Gaps - Critical for CVE Detection (P0)

| Gap | CVE Example | Description |
|-----|-------------|-------------|
| Discriminant correlation | CVE-2022-41033 | External tag field not tracked |
| Type punning | COMFUSION | Ptr/int confusion not caught |
| Cross-function | BleedingTooth | State lost at function boundary |
| Size correlation | Buffer overflow | Size fields not tracked |

### Test Files (CVE-based)

| File | Pattern | Status |
|------|---------|--------|
| `1000_union_cve_discriminant_xfail` | COM+ Event System | Gap |
| `1001_union_cve_ptr_int_fail` | COMFUSION ptr/int | Caught |
| `1002_union_cve_kernel_xfail` | BleedingTooth | Gap |
| `1003_union_cve_browser_fail` | V8/Flash | Caught |
| `1004_union_cve_parser_xfail` | Parsers | Gap |
| `1005_union_cve_size_fail` | Size confusion | Caught |
| `1006_union_cve_safe_pass` | Safe patterns | Pass |

---

## Gap Closure Plan

### P0: Critical (Next Sprint)

| # | Gap | Type | Effort |
|---|-----|------|--------|
| 1 | Monadic operations | expected | 1-2 weeks |
| 2 | Discriminant correlation | union | 2-3 weeks |

### P1: High Priority

| # | Gap | Type | Effort |
|---|-----|------|--------|
| 3 | Switch case proofs | variant | 1 week |
| 4 | Reference invalidation | variant/any | 2 weeks |
| 5 | Cross-function tracking | union | 1-2 weeks |

### P2: Medium Priority

| # | Gap | Type | Effort |
|---|-----|------|--------|
| 6 | Type punning detection | union | 1 week |
| 7 | Moved-from state | any/variant | 1 week |

---

## Implementation Details

### Discriminant Correlation (Union P0)

**Problem:** Cannot correlate external tag field with union access.

```cpp
struct TaggedUnion {
    int tag;
    union { int i; float f; } data;
};

void process(TaggedUnion *t) {
    if (t->tag == TAG_INT) {
        int x = t->data.i;  // DTT cannot prove safe
    }
}
```

**Solution:**
1. Detect struct containing enum/int + union
2. Track `struct._tag_field` → `struct.union._union_active` correlation
3. Extract proofs from `if (s.tag == CONSTANT)`
4. Map enum constants to union members

### Monadic Operations (Expected P0)

**Problem:** State lost through chains.

```cpp
auto result = get_data()
    .and_then(parse)      // _has_value lost
    .transform(validate); // _has_value lost
```

**Solution:**
- `and_then(F)`: output has value iff F returns value
- `transform(F)`: output has value iff input has value
- `or_else(F)`: output depends on F return
- `transform_error(F)`: output has error iff input has error

### Switch Case Proofs (Variant P1)

**Problem:** No proof in switch cases.

```cpp
switch(v.index()) {
    case 0: std::get<0>(v); break;  // Should be provably safe
    case 1: std::get<1>(v); break;
}
```

**Solution:**
1. Detect `switch(v.index())`
2. Add proof `v._variant_index == case_value` per case
3. Handle `default:` as unknown

### Reference Invalidation (Variant/Any P1)

**Problem:** Dangling refs not detected.

```cpp
auto& vec = std::get<std::vector<int>>(v);
v = 42;           // vec is now dangling
vec.push_back(4); // USE-AFTER-FREE not caught
```

**Solution:**
1. Track `ref._depends_on = "variant_path"`
2. On reassignment, mark dependent refs as `_invalidated = 1`
3. Check `_invalidated != 1` before use

---

## Architecture Notes

### Why Hardcoded, Not Annotation-Based

DTT's annotation system supports integer properties only. Sum type tracking requires:
- Type inference from constructor arguments
- Template argument extraction from `get<T>`
- QualType storage for any's contained type
- Type comparison for any_cast validation

These are hardcoded in DTTPlugin.cpp (~600 lines).

### User-Defined Sum Types (Future)

Not currently supported. Would require annotation extensions:

```cpp
[[clang::annotate("dtt:sum_type=_index")]]
class MyVariant { ... };

[[clang::annotate("dtt:sum_get=_index == $tparam0")]]
template<size_t I> auto& get(MyVariant& v);
```

---

## Test File Reference

### std::variant
- `80-85_*` - Basic index/holds/get_if tests
- `88_variant_visit_pass` - std::visit
- `830_std_variant_pass` - Comprehensive
- `832c_variant_emplace_pass` - emplace tracking
- `832d_*_silent` - Gap: ref invalidation
- `832g_*_xfail` - Gap: switch proofs

### std::any
- `90-91_*` - Basic any_cast tests
- `832_std_any_pass` - Comprehensive
- `833b_any_else_branch_fail` - Else branch

### std::expected
- `996_expected_pass` - Safe patterns
- `997_expected_deref_fail` - Dereference without value
- `998_expected_error_fail` - error() with value

### Union (CVE-based)
- `994-995_union_*` - Basic union tests
- `1000-1006_union_cve_*` - CVE patterns
