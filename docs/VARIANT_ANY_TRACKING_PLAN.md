# Implementation Plan: std::variant and std::any Type Tracking

## Executive Summary

This plan extends DTT to track the **active type** inside `std::variant` and `std::any`, enabling detection of:
- Type mismatch errors (`std::get<T>` on wrong alternative)
- Reference invalidation after variant reassignment
- Iterator/pointer invalidation inside type-erased containers

## Current State

### What DTT Already Has
- `_has_value` tracking for `std::any` and `std::optional`
- `std::any_cast` checks `_has_value` before access
- Template parameter extraction infrastructure
- Method annotation system with preconditions/postconditions
- Proof system for flow-sensitive guards

### What's Missing
- **Variant**: No `_variant_index` tracking, no `std::get` validation
- **Any**: No contained type tracking (only emptiness)
- **Both**: No reference invalidation detection after reassignment

---

## Phase 1: Basic Variant Index Tracking

**Goal**: Track which alternative is active in a `std::variant`

### 1.1 New Property: `_variant_index`

Add tracking for the active alternative index:

```cpp
// In DTTStdLib.cpp - MethodAnnotations
{"std::variant", "variant", 0,  // Default constructor
 {}, {{".ensures", "_variant_index", "0"}}},  // First alternative is active

{"std::variant", "emplace", -1,  // emplace<I>(args...)
 {}, {{".ensures", "_variant_index", "$tparam0"}}},  // Template param is index
```

### 1.2 Track Assignment

When assigning to a variant, update `_variant_index`:

```cpp
std::variant<int, std::string, double> v;
v = 42;           // _variant_index = 0 (int)
v = "hello";      // _variant_index = 1 (string)
v = 3.14;         // _variant_index = 2 (double)
```

**Implementation**: In `VisitCXXOperatorCallExpr` for `operator=`:
1. Detect RHS type
2. Look up which variant alternative matches
3. Set `_variant_index` accordingly

### 1.3 Handle `std::get<I>(v)`

Add precondition checking:

```cpp
// Pseudo-annotation
std::get<I>(variant<Ts...>& v):
    requires: v._variant_index == I
    returns: reference to alternative I
```

**Implementation**: In `VisitCallExpr`:
1. Detect calls to `std::get` with variant argument
2. Extract template parameter `I`
3. Check `v._variant_index == I` (or unknown → allow with warning?)
4. Report error if mismatch proven

### 1.4 Handle `std::holds_alternative<T>(v)`

Create proofs from conditionals:

```cpp
if (std::holds_alternative<std::string>(v)) {
    // Proof: v._variant_index == 1
    auto& s = std::get<std::string>(v);  // Safe
}
```

**Implementation**: In guard extraction (`VisitIfStmt`):
1. Detect `std::holds_alternative<T>(v)` in condition
2. Map `T` to index in variant's template parameters
3. Add proof `v._variant_index == index` in true branch
4. Add proof `v._variant_index != index` in false branch

### 1.5 Handle `v.index()`

Track return value for comparisons:

```cpp
if (v.index() == 1) {
    // Proof: v._variant_index == 1
    auto& s = std::get<1>(v);  // Safe
}
```

**Implementation**: Similar to `holds_alternative`, but with direct index comparison.

---

## Phase 2: Type-Based Variant Access

**Goal**: Support `std::get<T>(v)` where T is a type, not an index

### 2.1 Type-to-Index Mapping

At variant construction/declaration, build a map:

```cpp
std::variant<int, std::string, double> v;
// TypeMap: {int→0, std::string→1, double→2}
```

Store in a separate structure keyed by variant variable.

### 2.2 Transform Type Access to Index Access

When seeing `std::get<std::string>(v)`:
1. Look up `std::string` in v's TypeMap → index 1
2. Check `v._variant_index == 1`

### 2.3 Handle `std::get_if<T>(&v)`

Returns pointer (nullptr if wrong type):

```cpp
if (auto* s = std::get_if<std::string>(&v)) {
    // s is non-null, v._variant_index == 1
    s->size();  // Safe
}
// Outside: s might be null
```

**Implementation**:
1. Return value has `_nonnull` property
2. In true branch of `if (auto* s = std::get_if...)`:
   - Set `s._nonnull = 1`
   - Set proof `v._variant_index == type_index`

---

## Phase 3: Reference Invalidation Tracking

**Goal**: Detect dangling references after variant reassignment

### 3.1 The Problem

```cpp
std::variant<int, std::vector<int>> v = std::vector<int>{1, 2, 3};
auto& vec = std::get<std::vector<int>>(v);  // Reference to internal vector

v = 42;  // REASSIGNMENT - vec is now dangling!
vec.push_back(4);  // USE-AFTER-FREE
```

### 3.2 Track Reference Dependencies

When extracting a reference via `std::get`:

```cpp
auto& vec = std::get<std::vector<int>>(v);
// Track: vec._depends_on = "v"
// Track: vec._valid_while = v._variant_index (current value)
```

### 3.3 Invalidate on Reassignment

When variant is reassigned:

```cpp
v = 42;  // v._variant_index changes
// All references with vec._depends_on == "v" are now invalid
// Set: vec._invalidated = 1
```

### 3.4 Check on Use

When using a potentially invalidated reference:

```cpp
vec.push_back(4);
// Check: vec._invalidated != 1
// Error: "reference 'vec' was invalidated when 'v' was reassigned at line X"
```

### 3.5 Implementation Strategy

**Option A: Eager Invalidation**
- On variant reassignment, scan all tracked variables
- Mark any that depend on the variant as invalid
- Simple but potentially expensive

**Option B: Lazy Validation**
- Track the "version" of the variant when reference was taken
- On reference use, check if variant version changed
- More efficient but requires version tracking

**Recommended: Option A** for initial implementation (simpler, correctness first).

---

## Phase 4: std::visit Support

**Goal**: Handle `std::visit` correctly (it's always type-safe)

### 4.1 Basic Handling

`std::visit` is always safe - the visitor is called with the correct type:

```cpp
std::visit([](auto& x) {
    // x is always valid, type determined by variant
}, v);
```

**Implementation**: No preconditions needed for `std::visit` itself.

### 4.2 Track Mutations Inside Visitor

If visitor modifies the variant:

```cpp
std::visit([&v](auto& x) {
    v = 42;  // Modifies v during visit!
    // x might now be invalid (implementation-defined)
}, v);
```

This is tricky - for now, flag as warning if variant is modified inside its own visitor.

---

## Phase 5: std::any Type Tracking (Bonus)

**Goal**: Track the contained type in `std::any`, not just emptiness

### 5.1 New Property: `_any_type_id`

Store a numeric type ID representing the contained type:

```cpp
std::any a = 42;           // _any_type_id = hash("int")
a = std::string{"hello"};  // _any_type_id = hash("std::string")
a.reset();                 // _any_type_id = 0, _has_value = 0
```

### 5.2 Track at Assignment

In `VisitCXXOperatorCallExpr` and construction:
1. Get the type of the RHS
2. Compute type ID (hash or enum)
3. Set `a._any_type_id`

### 5.3 Validate `std::any_cast<T>`

```cpp
std::any a = 42;
auto s = std::any_cast<std::string>(a);  // ERROR: type mismatch
```

**Implementation**: In `VisitCallExpr` for `std::any_cast`:
1. Extract requested type `T`
2. Compute type ID for `T`
3. Check `a._any_type_id == type_id(T)`
4. Report error on proven mismatch

### 5.4 Type Guards from `a.type()`

```cpp
if (a.type() == typeid(int)) {
    auto i = std::any_cast<int>(a);  // Safe
}
```

**Implementation**: Extract proof from `a.type() == typeid(T)` conditions.

### 5.5 Limitation: Dynamic Type IDs

Unlike `variant`, `any` can hold ANY type. Type IDs must be:
- Computed at compile time from type name
- Possibly limited to "known" types (registered in DTT)
- Unknown types → conservative (allow access, can't prove error)

---

## Implementation Order

### Sprint 1: Foundation (Variant Index)
1. [ ] Add `_variant_index` property infrastructure
2. [ ] Track index on variant construction
3. [ ] Track index on variant assignment (`operator=`)
4. [ ] Handle `std::get<I>(v)` - index-based access
5. [ ] Write tests: `80_variant_index_pass.cpp`, `81_variant_index_fail.cpp`

### Sprint 2: Type-Based Access
6. [ ] Build type-to-index map at construction
7. [ ] Handle `std::get<T>(v)` - type-based access
8. [ ] Handle `std::holds_alternative<T>(v)` - proof extraction
9. [ ] Handle `v.index()` comparison - proof extraction
10. [ ] Write tests: `82_variant_holds_pass.cpp`, `83_variant_holds_fail.cpp`

### Sprint 3: get_if and Pointer Access
11. [ ] Handle `std::get_if<T>(&v)` - pointer return
12. [ ] Track nullability based on type match
13. [ ] Extract proofs from `if (auto* p = std::get_if...)` pattern
14. [ ] Write tests: `84_variant_getif_pass.cpp`, `85_variant_getif_fail.cpp`

### Sprint 4: Reference Invalidation
15. [ ] Track reference dependencies on variant
16. [ ] Invalidate references on variant reassignment
17. [ ] Report use-after-invalidation errors
18. [ ] Write tests: `86_variant_ref_pass.cpp`, `87_variant_ref_fail.cpp`

### Sprint 5: std::visit
19. [ ] Mark `std::visit` as always-safe access
20. [ ] Warn on variant modification inside visitor
21. [ ] Write tests: `88_variant_visit_pass.cpp`

### Sprint 6: std::any Type Tracking (Bonus)
22. [ ] Add `_any_type_id` property
23. [ ] Track type on assignment
24. [ ] Validate `std::any_cast<T>` type match
25. [ ] Extract proofs from `a.type() == typeid(T)`
26. [ ] Write tests: `90_any_type_pass.cpp`, `91_any_type_fail.cpp`

---

## Test Matrix

| Test File | Scenario | Expected |
|-----------|----------|----------|
| `80_variant_index_pass.cpp` | Correct index access | Pass |
| `81_variant_index_fail.cpp` | Wrong index access | Fail (DTT error) |
| `82_variant_holds_pass.cpp` | Access after holds_alternative check | Pass |
| `83_variant_holds_fail.cpp` | Access without check | Fail |
| `84_variant_getif_pass.cpp` | Null check before use | Pass |
| `85_variant_getif_fail.cpp` | No null check | Fail |
| `86_variant_ref_pass.cpp` | Reference used before reassign | Pass |
| `87_variant_ref_fail.cpp` | Reference used after reassign | Fail |
| `88_variant_visit_pass.cpp` | std::visit usage | Pass |
| `90_any_type_pass.cpp` | Correct any_cast | Pass |
| `91_any_type_fail.cpp` | Wrong any_cast type | Fail |

---

## Technical Details

### Detecting std::get Calls

```cpp
bool isStdGet(const FunctionDecl *FD) {
    if (!FD) return false;
    auto QName = FD->getQualifiedNameAsString();
    // Handle std::__1::get (libc++) and std::get (libstdc++)
    return QName == "std::get" ||
           QName.find("std::") == 0 && QName.find("::get") != std::string::npos;
}
```

### Extracting Template Arguments from std::get<I>

```cpp
// In VisitCallExpr for std::get
if (auto *DRE = dyn_cast<DeclRefExpr>(CE->getCallee()->IgnoreImplicit())) {
    if (auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl())) {
        // Get template arguments
        if (auto *TAL = DRE->getTemplateArgs()) {
            // First arg is either a type or an integer
            if (TAL[0].getKind() == TemplateArgument::Integral) {
                int64_t index = TAL[0].getAsIntegral().getExtValue();
                // Check v._variant_index == index
            } else if (TAL[0].getKind() == TemplateArgument::Type) {
                QualType T = TAL[0].getAsType();
                // Look up T in variant's type list
            }
        }
    }
}
```

### Mapping Type to Variant Index

```cpp
// Given variant<int, string, double> and type string
// Return index 1

std::optional<size_t> getVariantIndexForType(
    const ClassTemplateSpecializationDecl *VD,
    QualType TargetType)
{
    const auto &TAL = VD->getTemplateArgs();
    for (size_t i = 0; i < TAL.size(); ++i) {
        if (TAL[i].getKind() == TemplateArgument::Type) {
            if (Ctx.hasSameType(TAL[i].getAsType(), TargetType)) {
                return i;
            }
        }
    }
    return std::nullopt;
}
```

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| libc++ inline namespaces | std::get not detected | Use namespace traversal (already solved for std::move) |
| Complex control flow | Imprecise index tracking | Conservative: unknown index allows access |
| Recursive variants | Infinite loops | Limit recursion depth |
| Many alternatives | Performance | Limit to first N alternatives |
| Template metaprogramming | Can't determine index | Fall back to unknown |

---

## Success Criteria

1. **Correctness**: No false positives on valid code
2. **Coverage**: Catch common variant misuse patterns
3. **Performance**: < 10% slowdown on large codebases
4. **Compatibility**: Works on both libstdc++ and libc++

---

## Future Extensions

- `std::expected<T, E>` tracking (C++23)
- `boost::variant` support
- `std::variant` with `std::monostate`
- Recursive variant types
- Variant inside containers (`vector<variant<...>>`)
