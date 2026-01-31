# Implementation Plan: std::expected Tracking (C++23)

## Executive Summary

This plan extends DTT to track the **value/error state** of `std::expected<T, E>`, enabling detection of:
- Calling `value()` when no value present (throws `bad_expected_access`)
- Using `operator*` or `operator->` when no value (undefined behavior)
- Calling `error()` when value is present (undefined behavior)

`std::expected` is C++23's modern error handling primitive, similar to Rust's `Result<T, E>`. It's a sum type that holds either a value of type `T` or an error of type `E`.

---

## Motivation

### Why std::expected Matters

| Aspect | Description |
|--------|-------------|
| **Adoption** | Rapidly replacing exception-based error handling in modern C++ |
| **Safety gap** | `operator*` and `error()` have UB preconditions (unlike `value()` which throws) |
| **Similar to optional** | Same tracking pattern as `std::optional` but with error state |
| **Monadic chaining** | `and_then`, `transform`, `or_else` create complex control flow |

### Comparison to Related Types

| Type | Has value? | Access value | Access error | DTT Status |
|------|------------|--------------|--------------|------------|
| `std::optional<T>` | `has_value()` | `value()`, `*` | N/A | ✓ Implemented |
| `std::variant<T,E>` | `index()` | `get<T>()` | `get<E>()` | ✓ Implemented |
| `std::expected<T,E>` | `has_value()` | `value()`, `*` | `error()` | ❌ **Planned** |

---

## Scope: What This WILL and WILL NOT Support

### WILL Support

| Feature | Description | Example | Priority |
|---------|-------------|---------|----------|
| **Value state tracking** | Track `_has_value` (0 or 1) | `e = T{}` → `_has_value = 1` | P0 |
| **value() validation** | Check before calling `value()` | `e.value()` requires `_has_value == 1` | P0 |
| **operator\* validation** | Check before dereferencing | `*e` requires `_has_value == 1` | P0 |
| **operator-> validation** | Check before member access | `e->member` requires `_has_value == 1` | P0 |
| **error() validation** | Check before calling `error()` | `e.error()` requires `_has_value == 0` | P0 |
| **Construction tracking** | Track from constructor | `expected<T,E> e{val}` → has value | P0 |
| **std::unexpected tracking** | Track error assignment | `e = std::unexpected{err}` → no value | P0 |
| **has_value() guards** | Proof extraction from checks | `if (e.has_value()) { *e; }` | P0 |
| **bool conversion guards** | Proof from `if (e)` | `if (e) { *e; }` | P0 |
| **value_or() safety** | Always safe, no check needed | `e.value_or(default)` | P0 |
| **Copy/move semantics** | Propagate state through copy/move | `auto e2 = e1;` | P1 |
| **emplace() tracking** | Track state after emplace | `e.emplace(args...)` → has value | P1 |
| **Monadic and_then** | Track through `and_then` chain | `e.and_then(f)` | P2 |
| **Monadic transform** | Track through `transform` | `e.transform(f)` | P2 |
| **Monadic or_else** | Track through `or_else` | `e.or_else(f)` | P2 |

### WILL NOT Support (Out of Scope)

| Feature | Reason | Workaround |
|---------|--------|------------|
| **Cross-function tracking** | Expected passed to/from functions | Annotations on function boundaries |
| **Expected in containers** | `vector<expected<...>>` element tracking | Track container, not elements |
| **Complex monadic chains** | `e.and_then(f).transform(g).or_else(h)` | Simplified single-step tracking |
| **Expected of references** | `expected<T&, E>` | Rare usage |
| **void expected** | `expected<void, E>` has different semantics | Partial support |
| **Error type conversion** | `expected<T, E1>` to `expected<T, E2>` | Track as unknown |

---

## API Reference

### Preconditions (from [cppreference](https://en.cppreference.com/w/cpp/utility/expected.html))

| Method | Precondition | On Violation |
|--------|--------------|--------------|
| `value()` | `has_value() == true` | Throws `bad_expected_access<E>` |
| `operator*` | `has_value() == true` | **Undefined behavior** |
| `operator->` | `has_value() == true` | **Undefined behavior** |
| `error()` | `has_value() == false` | **Undefined behavior** |
| `value_or(v)` | None | Returns value or `v` |
| `error_or(e)` | None | Returns error or `e` |

### State Transitions

```
                    ┌─────────────────────────────────────┐
                    │                                     │
                    ▼                                     │
┌──────────────────────────────────┐                      │
│  expected<T,E> e;                │ (default: has value) │
│  _has_value = 1                  │                      │
└──────────────────────────────────┘                      │
        │                                                 │
        │ e = std::unexpected{err}                        │
        ▼                                                 │
┌──────────────────────────────────┐                      │
│  _has_value = 0                  │                      │
│  (holds error)                   │                      │
└──────────────────────────────────┘                      │
        │                                                 │
        │ e = T{} or e.emplace(...)                       │
        └─────────────────────────────────────────────────┘
```

---

## Technical Design

### Property: `_has_value`

Reuse existing `_has_value` property (same as `std::optional`):

```cpp
std::expected<int, std::string> e;      // _has_value = 1 (default constructs T)
std::expected<int, std::string> e{42};  // _has_value = 1
std::expected<int, std::string> e{std::unexpected{"error"}};  // _has_value = 0

int x = e.value();   // Check: _has_value == 1
int y = *e;          // Check: _has_value == 1 (UB if not)
auto& err = e.error();  // Check: _has_value == 0 (UB if not)
```

### Detection Points

#### 1. Construction

```cpp
// In VisitVarDecl or VisitCXXConstructExpr
if (isStdExpected(VD->getType())) {
    if (auto *Init = VD->getInit()) {
        if (isStdUnexpected(Init->getType())) {
            // Constructed with error
            setFieldTyped(varPath, "_has_value", SymValue::Const(0));
        } else {
            // Constructed with value (or default)
            setFieldTyped(varPath, "_has_value", SymValue::Const(1));
        }
    } else {
        // Default construction - has value (T is default-constructed)
        setFieldTyped(varPath, "_has_value", SymValue::Const(1));
    }
}
```

#### 2. Assignment

```cpp
// In VisitCXXOperatorCallExpr or VisitBinaryOperator
if (isAssignmentToExpected(Op)) {
    Expr *RHS = getRHS(Op);
    if (isStdUnexpected(RHS->getType())) {
        setFieldTyped(lhsPath, "_has_value", SymValue::Const(0));
    } else {
        setFieldTyped(lhsPath, "_has_value", SymValue::Const(1));
    }
}
```

#### 3. value() Call

```cpp
// In VisitCXXMemberCallExpr
if (isExpectedValueCall(MCE)) {
    FieldPath objPath = getTypedPath(MCE->getImplicitObjectArgument());
    SymValue hasValue = getFieldTyped(objPath, "_has_value");

    // value() throws if no value - we can warn but it's not UB
    if (hasValue.isConst() && hasValue.getConstValue() == 0) {
        reportWarning(MCE->getBeginLoc(),
            "calling value() on expected without value - will throw bad_expected_access");
    }
}
```

#### 4. operator* and operator->

```cpp
// In VisitCXXOperatorCallExpr for unary * or in VisitMemberExpr for ->
if (isExpectedDereference(Op)) {
    FieldPath objPath = getTypedPath(getOperand(Op));
    SymValue hasValue = getFieldTyped(objPath, "_has_value");

    // Dereference is UB if no value
    if (hasValue.isConst() && hasValue.getConstValue() == 0) {
        reportError(Op->getBeginLoc(),
            "dereferencing expected without value is undefined behavior");
    } else if (!emitCategoryC(Op->getBeginLoc(), pathStr, "_has_value", "_has_value == 1")) {
        emitCategoryD(Op->getBeginLoc(), pathStr, "_has_value",
                      "_has_value == 1", "expected state unknown");
    }
}
```

#### 5. error() Call

```cpp
// In VisitCXXMemberCallExpr
if (isExpectedErrorCall(MCE)) {
    FieldPath objPath = getTypedPath(MCE->getImplicitObjectArgument());
    SymValue hasValue = getFieldTyped(objPath, "_has_value");

    // error() is UB if has value
    if (hasValue.isConst() && hasValue.getConstValue() == 1) {
        reportError(MCE->getBeginLoc(),
            "calling error() on expected with value is undefined behavior");
    } else if (!emitCategoryC(...)) {
        emitCategoryD(MCE->getBeginLoc(), pathStr, "_has_value",
                      "_has_value == 0", "expected state unknown");
    }
}
```

#### 6. Guard Extraction

```cpp
// In VisitIfStmt - handle has_value() and bool conversion
if (isExpectedHasValueCall(Cond) || isExpectedBoolConversion(Cond)) {
    FieldPath expectedPath = getExpectedFromCondition(Cond);

    // True branch: _has_value == 1
    addProof(TrueBranch, expectedPath, "_has_value", ProofOp::EQ, 1);

    // False branch: _has_value == 0
    addProof(FalseBranch, expectedPath, "_has_value", ProofOp::EQ, 0);
}
```

---

## Helper Functions

### isStdExpected

```cpp
bool isStdExpected(QualType T) {
    T = T.getCanonicalType().getNonReferenceType();
    if (auto *TST = T->getAs<TemplateSpecializationType>()) {
        if (auto *TD = TST->getTemplateName().getAsTemplateDecl()) {
            if (TD->getIdentifier() && TD->getName() == "expected") {
                return isInStdNamespace(TD);
            }
        }
    }
    if (auto *RD = T->getAsCXXRecordDecl()) {
        if (RD->getIdentifier() && RD->getName() == "expected") {
            return isInStdNamespace(RD);
        }
    }
    return false;
}
```

### isStdUnexpected

```cpp
bool isStdUnexpected(QualType T) {
    T = T.getCanonicalType().getNonReferenceType();
    if (auto *RD = T->getAsCXXRecordDecl()) {
        if (RD->getIdentifier() && RD->getName() == "unexpected") {
            return isInStdNamespace(RD);
        }
    }
    return false;
}
```

### isExpectedValueCall

```cpp
bool isExpectedValueCall(CXXMemberCallExpr *MCE) {
    if (auto *MD = MCE->getMethodDecl()) {
        if (MD->getIdentifier() && MD->getName() == "value") {
            return isStdExpected(MCE->getImplicitObjectArgument()->getType());
        }
    }
    return false;
}
```

### isExpectedErrorCall

```cpp
bool isExpectedErrorCall(CXXMemberCallExpr *MCE) {
    if (auto *MD = MCE->getMethodDecl()) {
        if (MD->getIdentifier() && MD->getName() == "error") {
            return isStdExpected(MCE->getImplicitObjectArgument()->getType());
        }
    }
    return false;
}
```

---

## Diagnostics

### Error Messages

```
error: dereferencing std::expected without value is undefined behavior
  --> src/result.cpp:42:15
   |
42 |     int x = *result;
   |             ^~~~~~~
   |
note: 'result' was assigned std::unexpected here
  --> src/result.cpp:40:5
   |
40 |     result = std::unexpected{parse_error::invalid};
   |     ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
help: check has_value() before dereferencing, or use value_or()
```

```
error: calling error() on std::expected with value is undefined behavior
  --> src/result.cpp:50:20
   |
50 |     auto err = result.error();
   |                       ^~~~~
   |
note: 'result' contains a value (assigned here)
  --> src/result.cpp:48:5
   |
48 |     result = 42;
   |     ^~~~~~~~~~~
```

```
warning: calling value() on std::expected without value - will throw bad_expected_access
  --> src/result.cpp:60:15
   |
60 |     int x = result.value();
   |                    ^~~~~
   |
note: 'result' was assigned std::unexpected here
  --> src/result.cpp:58:5
```

---

## Implementation Phases

### Phase 1: Core State Tracking (P0)

**Goal:** Track `_has_value` and validate access methods

1. Detect `std::expected` type (handle libc++/libstdc++ variations)
2. Track state on construction
3. Track state on `std::unexpected` assignment
4. Validate `operator*` and `operator->` (UB check)
5. Validate `error()` call (UB check)
6. Warn on `value()` without value (throws, not UB)
7. Extract guards from `has_value()` and `if (e)`

**Tests:**
- `860_expected_pass.cpp` - Correct access patterns
- `861_expected_deref_fail.cpp` - Dereference without value
- `862_expected_error_fail.cpp` - error() with value
- `863_expected_value_warn.cpp` - value() without value (warning)

**Estimated scope:** ~200 lines of code

### Phase 2: Enhanced Tracking (P1)

**Goal:** Copy semantics and emplace

8. Propagate `_has_value` through copy/move
9. Track `emplace()` - sets has_value = 1
10. Handle `value_or()` and `error_or()` - always safe

**Tests:**
- `864_expected_copy_pass.cpp`
- `865_expected_emplace_pass.cpp`

**Estimated scope:** ~100 lines of code

### Phase 3: Monadic Operations (P2)

**Goal:** Track state through monadic chaining

11. Handle `and_then()` - value required, result depends on function
12. Handle `transform()` - value required, result has value if input did
13. Handle `or_else()` - error required, may recover to value
14. Handle `transform_error()` - error required, still error after

**Tests:**
- `866_expected_and_then_pass.cpp`
- `867_expected_transform_pass.cpp`
- `868_expected_chain_fail.cpp`

**Estimated scope:** ~200 lines of code

---

## Test Cases

### 860_expected_pass.cpp (Safe Patterns)

```cpp
#include <expected>
#include <string>

std::expected<int, std::string> parse(const char* s);

void test_guarded_access() {
    auto result = parse("42");

    // Guard with has_value()
    if (result.has_value()) {
        int x = *result;      // OK - guarded
        int y = result.value();  // OK - guarded
    }

    // Guard with bool conversion
    if (result) {
        int x = *result;      // OK - guarded
    }

    // Guard with negation
    if (!result) {
        auto& err = result.error();  // OK - guarded (no value)
    }
}

void test_value_or() {
    auto result = parse("invalid");
    int x = result.value_or(0);  // Always safe
}

void test_construction() {
    std::expected<int, std::string> e1{42};
    int x = *e1;  // OK - constructed with value

    std::expected<int, std::string> e2{std::unexpected{"error"}};
    auto& err = e2.error();  // OK - constructed with error
}

void test_reassignment() {
    std::expected<int, std::string> e{42};
    int x = *e;  // OK - has value

    e = std::unexpected{"error"};
    auto& err = e.error();  // OK - now has error

    e = 100;
    int y = *e;  // OK - has value again
}
```

### 861_expected_deref_fail.cpp (UB: Dereference Without Value)

```cpp
#include <expected>
#include <string>

void test_deref_without_value() {
    std::expected<int, std::string> e{std::unexpected{"error"}};
    int x = *e;  // ERROR: dereferencing expected without value is UB
}

void test_arrow_without_value() {
    struct S { int val; };
    std::expected<S, std::string> e{std::unexpected{"error"}};
    int x = e->val;  // ERROR: using operator-> on expected without value is UB
}

void test_after_error_assignment() {
    std::expected<int, std::string> e{42};
    e = std::unexpected{"error"};
    int x = *e;  // ERROR: dereferencing expected without value is UB
}
```

### 862_expected_error_fail.cpp (UB: error() With Value)

```cpp
#include <expected>
#include <string>

void test_error_with_value() {
    std::expected<int, std::string> e{42};
    auto& err = e.error();  // ERROR: calling error() on expected with value is UB
}

void test_error_after_value_assignment() {
    std::expected<int, std::string> e{std::unexpected{"error"}};
    e = 42;  // Now has value
    auto& err = e.error();  // ERROR: calling error() on expected with value is UB
}
```

### 863_expected_value_warn.cpp (Throws: value() Without Value)

```cpp
#include <expected>
#include <string>

void test_value_without_value() {
    std::expected<int, std::string> e{std::unexpected{"error"}};
    int x = e.value();  // WARNING: will throw bad_expected_access
    // Note: This is not UB, but will throw - worth a warning
}
```

### 866_expected_and_then_pass.cpp (Monadic Operations)

```cpp
#include <expected>
#include <string>

std::expected<int, std::string> parse(const char* s);
std::expected<double, std::string> to_double(int x);

void test_and_then() {
    auto result = parse("42").and_then(to_double);

    if (result) {
        double d = *result;  // OK - guarded
    }
}

void test_transform() {
    auto result = parse("42").transform([](int x) { return x * 2; });

    if (result) {
        int x = *result;  // OK - guarded
    }
}

void test_or_else() {
    auto result = parse("invalid").or_else([](const std::string& err) {
        return std::expected<int, std::string>{0};  // Recovery
    });

    // Result could have value (recovery) or error
    if (result) {
        int x = *result;  // OK - guarded
    }
}
```

---

## Edge Cases

### Default Construction

```cpp
std::expected<int, std::string> e;  // Default constructs T (int)
// _has_value = 1
int x = *e;  // OK (value is 0)
```

### expected<void, E>

```cpp
std::expected<void, std::string> e;  // Special case
// _has_value = 1 (void is "present")
// No operator* - can't dereference void
e.value();  // OK - returns void
```

**Handling:** Track `_has_value` normally, but `operator*` is not applicable.

### Nested expected

```cpp
std::expected<std::expected<int, E1>, E2> e;
// Track outer expected only
```

### expected with reference (Illegal in C++23)

```cpp
// std::expected<int&, E> e;  // Not allowed
```

---

## Integration with Existing Tracking

### Similarity to std::optional

`std::expected` tracking is nearly identical to `std::optional`:

| Feature | std::optional | std::expected |
|---------|---------------|---------------|
| Property | `_has_value` | `_has_value` |
| value() | Check, throws | Check, throws |
| operator* | Check, UB | Check, UB |
| error() | N/A | Check, UB (new) |
| value_or() | Safe | Safe |

**Code reuse:** Factor out common `_has_value` checking logic.

### Relationship to std::variant

`std::expected<T, E>` is conceptually `std::variant<T, E>` with:
- Index 0 = has value
- Index 1 = has error

Could potentially unify tracking, but the API differences (error() vs get<E>()) make separate handling cleaner.

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| libc++ inline namespaces | std::expected not detected | Use namespace traversal (existing pattern) |
| tl::expected (pre-C++23) | Popular library not supported | Add as separate detection |
| Monadic chains | Complex control flow | P2, conservative tracking |
| void expected | Different API | Special case handling |

---

## Success Criteria

1. **Zero false positives** on correct expected access
2. **Catch UB** from `operator*` and `error()` misuse
3. **Warn** on `value()` without value (throws, not UB)
4. **Handle guards** from `has_value()` and bool conversion
5. **Performance** < 3% overhead (expected is used in hot paths)

---

## References

- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected.html)
- [C++ Stories: Using std::expected from C++23](https://www.cppstories.com/2024/expected-cpp23/)
- [Andreas Fertig: C++23 std::expected](https://andreasfertig.com/blog/2022/09/cpp23-stdexpected-the-superior-way-of-returning-a-value-or-an-error/)
- [tl::expected](https://tl.tartanllama.xyz/en/latest/api/expected.html) - Pre-C++23 implementation
