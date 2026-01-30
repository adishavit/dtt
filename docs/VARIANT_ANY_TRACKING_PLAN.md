# Implementation Plan: std::variant and std::any Type Tracking

## Executive Summary

This plan extends DTT to track the **active type** inside `std::variant` and `std::any`, enabling detection of:
- Type mismatch errors (`std::get<T>` on wrong alternative)
- Reference invalidation after variant reassignment
- Iterator/pointer invalidation inside type-erased containers

---

## Scope: What This WILL and WILL NOT Support

### std::variant - WILL Support

| Feature | Description | Example | Status |
|---------|-------------|---------|--------|
| **Index tracking** | Track which alternative (0, 1, 2...) is active | `v = "hello"` → `_variant_index = 1` | ✓ Implemented |
| **std::get\<I\> validation** | Error if accessing wrong index | `std::get<0>(v)` when index is 1 | ❌ **GAP** (see 832e_*_silent.cpp) |
| **std::get\<T\> validation** | Error if accessing wrong type | `std::get<int>(v)` when holds string | ✓ Implemented |
| **holds_alternative guards** | Proof extraction from conditionals | `if (holds_alternative<T>(v))` | ✓ Implemented |
| **v.index() guards** | Proof extraction from index checks | `if (v.index() == 1)` | ✓ Implemented |
| **std::get_if null tracking** | Track nullptr when type mismatches | `if (auto* p = std::get_if<T>(&v))` | ✓ Implemented |
| **Reference invalidation** | Detect dangling refs after reassignment | `auto& x = std::get<T>(v); v = other;` | ❌ **GAP** (see 832d_*_silent.cpp) |
| **std::visit** | Mark as always-safe (no preconditions) | `std::visit(visitor, v)` | ✓ Implemented |
| **Copy/move semantics** | Track index through copies and moves | `auto v2 = v; auto v3 = std::move(v);` | ✓ Implemented |
| **emplace tracking** | Update index on `v.emplace<I>()` | `v.emplace<1>("hello")` | ❌ **GAP** (see 832c_*_xfail.cpp) |
| **Else branch guards (correct)** | Access correct type in else after guard | `if (holds<A>(v)) {} else { get<B>(v); }` | ✓ Implemented |
| **Else branch guards (wrong)** | Catch wrong type access in else branch | `if (holds<A>(v)) {} else { get<A>(v); }` | ❌ **GAP** (see 832f_*_silent.cpp) |
| **Swap tracking** | Track index exchange on swap | `std::swap(v1, v2)` | ✓ Implemented (moved from WILL NOT) |

### std::variant - WILL NOT Support (Out of Scope)

| Feature | Reason | Workaround |
|---------|--------|------------|
| **Recursive variants** | Requires complex type graph analysis | Use simple flat variants |
| **Variants in containers** | `vector<variant<...>>` element tracking | Track container, not element types |
| **Cross-function tracking** | Variant passed to/from functions | Annotations on function boundaries |
| **valueless_by_exception** | Rare edge case, complex semantics | Assume never valueless |
| **Multiple variants in visit** | `std::visit(f, v1, v2)` cartesian product | Single-variant visit only |
| **Variant of references** | `variant<int&, string&>` illegal anyway | N/A |
| **CTAD (deduction guides)** | `variant v = 42` type deduction | Explicit type specification |
| **Comparison operators** | `v1 == v2`, `v1 < v2` | Not safety-critical |
| **Pointer-to-variant** | `variant<...>* pv` indirection | Track direct variants only |
| **Variant in union** | Undefined behavior territory | Don't do this |

### std::any - WILL Support

| Feature | Description | Example | Status |
|---------|-------------|---------|--------|
| **Type tracking** | Track what type is currently stored | `a = 42` → `_any_type = "int"` | ✓ Implemented |
| **any_cast validation** | Error if casting to wrong type | `any_cast<string>(a)` when holds int | ✓ Implemented |
| **has_value tracking** | Already implemented, enhanced | `a.has_value()` guards | ✓ Implemented |
| **type() guards (true branch)** | Proof from `a.type() == typeid(T)` | `if (a.type() == typeid(int))` | ✓ Implemented |
| **type() guards (else branch)** | Catch wrong cast in else after type check | `if (a.type() == typeid(int)) {} else { any_cast<int>(a); }` | ❌ **GAP** (see 833b_*_silent.cpp) |
| **Reference any_cast** | Track `any_cast<T&>(a)` lifetime | Tied to `a`'s lifetime | ✓ Implemented |
| **Pointer any_cast** | Track `any_cast<T*>(&a)` nullability | nullptr if wrong type | ✓ Implemented |
| **reset() tracking** | Clear type on `a.reset()` | `_any_type = none`, `_has_value = 0` | ✓ Implemented |
| **make_any** | Track type from `std::make_any<T>()` | Same as direct construction | ✓ Implemented |

### std::any - WILL NOT Support (Out of Scope)

| Feature | Reason | Workaround |
|---------|--------|------------|
| **User-defined types** | Infinite type space | Only track "known" stdlib types |
| **Type aliases** | `using MyInt = int; any a = MyInt{}` | Resolve to canonical type |
| **Polymorphic types** | `any a = Derived{}; any_cast<Base*>` | Track exact type only |
| **any in containers** | `vector<any>` element tracking | Track container, not elements |
| **Cross-function tracking** | `any` passed to/from functions | Conservative: unknown type |
| **Type-erased callbacks** | `any` holding `function<...>` | Not safety-critical |
| **Small object optimization** | Implementation detail | Irrelevant to safety |
| **Custom allocators** | Rare usage | Ignore allocator parameter |

### Fundamental Limitations

1. **Static analysis boundaries**: We can only track what's visible at compile time
2. **Conservative on unknown**: If we can't prove the type, we allow access (no false positives)
3. **Single translation unit**: No cross-TU tracking
4. **No runtime information**: Can't track types assigned via runtime conditionals from external input

### Implementation Gap Summary

The following features are planned but not yet fully implemented:

| Gap | Type | Test File | Description |
|-----|------|-----------|-------------|
| **emplace tracking** | xfail (false positive) | `832c_variant_emplace_xfail.cpp` | DTT loses tracking after `v.emplace<T>()`, causing errors on valid access |
| **Reference invalidation** | silent (false negative) | `832d_variant_ref_invalidation_silent.cpp` | DTT doesn't detect dangling refs after variant reassignment |
| **Index-based get** | silent (false negative) | `832e_variant_index_get_silent.cpp` | DTT validates `std::get<Type>` but NOT `std::get<Index>` |
| **Wrong else branch (variant)** | silent (false negative) | `832f_variant_else_branch_silent.cpp` | DTT doesn't catch wrong type in else branch after guard |
| **Wrong else branch (any)** | silent (false negative) | `833b_any_else_branch_silent.cpp` | DTT doesn't catch wrong cast in else after `a.type() == typeid(T)` |

**Gap types:**
- **xfail**: False positive - DTT errors on safe code (compilation fails when it should succeed)
- **silent**: False negative - DTT silent on unsafe code (compilation succeeds when it should fail)

---

## Current State

### What DTT Has (Implemented)
- `_variant_index` tracking for `std::variant`
- `_any_type` tracking for `std::any` (QualType-based)
- `std::get<Type>` validation with type-to-index mapping
- `std::any_cast` validation with type matching
- `std::holds_alternative` and `v.index()` guards (true branch)
- `std::get_if` null tracking
- Copy/move/swap semantics for variant index
- Template parameter extraction infrastructure
- Method annotation system with preconditions/postconditions
- Proof system for flow-sensitive guards

### What's Still Missing (See Gap Summary Above)
- `std::get<Index>` validation (index-based access not checked)
- `emplace` tracking (loses tracking on emplace calls)
- Reference invalidation detection after reassignment
- Wrong-branch detection in else clauses after guards

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

## Phase 5: std::any Type Tracking (Detailed)

**Goal**: Track the contained type in `std::any`, not just emptiness

### 5.1 The Core Challenge: Unbounded Type Space

Unlike `std::variant<Ts...>` where types are statically enumerated, `std::any` can hold **any copyable type**. This creates a fundamental tracking challenge:

```cpp
std::any a = 42;              // Could be int
std::any b = std::string{};   // Could be string
std::any c = MyCustomClass{}; // Could be anything
```

**Solution**: Use a **symbolic type identifier** system.

### 5.2 Type Identifier Approach

#### Option A: String-Based Type Names (Recommended)

Store the canonical type name as a string property:

```cpp
// New property: _any_type (string identifier)
std::any a = 42;
// a._any_type = "int"
// a._has_value = 1

a = std::string{"hello"};
// a._any_type = "std::string"  (or "std::__1::basic_string<...>" on libc++)
// a._has_value = 1

a.reset();
// a._any_type = ""  (empty)
// a._has_value = 0
```

**Pros**:
- Works with any type
- Human-readable diagnostics
- No type registry needed

**Cons**:
- String comparison overhead
- Canonical name variations (libc++ vs libstdc++)

#### Option B: Numeric Type Hash

Use a compile-time hash of the type name:

```cpp
constexpr uint64_t typeHash(const char* name) {
    // FNV-1a or similar
}

// a._any_type_hash = typeHash("int")
```

**Pros**: Fast comparison
**Cons**: Hash collisions (rare), less readable errors

#### Option C: Enum for Known Types (Hybrid)

Define an enum for common types, fall back to string for others:

```cpp
enum KnownAnyType {
    ANY_TYPE_UNKNOWN = 0,
    ANY_TYPE_INT,
    ANY_TYPE_DOUBLE,
    ANY_TYPE_STRING,
    ANY_TYPE_VECTOR_INT,
    // ... common types
    ANY_TYPE_CUSTOM = 999  // Use string comparison
};
```

**Recommended**: Start with **Option A** (string-based) for correctness, optimize later if needed.

### 5.3 Tracking Type on Construction and Assignment

#### 5.3.1 Direct Construction

```cpp
std::any a = 42;
std::any b{std::string{"hello"}};
std::any c(3.14);
```

**Implementation** in `VisitVarDecl`:

```cpp
if (isStdAny(VD->getType())) {
    if (auto* Init = VD->getInit()) {
        QualType storedType = Init->getType();
        std::string typeName = storedType.getCanonicalType().getAsString();
        setField(path, "_any_type", SymValue::String(typeName));
        setField(path, "_has_value", SymValue::Const(1));
    } else {
        // Default construction: empty
        setField(path, "_any_type", SymValue::String(""));
        setField(path, "_has_value", SymValue::Const(0));
    }
}
```

#### 5.3.2 Assignment

```cpp
std::any a;
a = 42;           // _any_type = "int"
a = "hello";      // _any_type = "const char*" (NOT std::string!)
a = std::string{"hello"};  // _any_type = "std::string"
```

**Implementation** in `VisitCXXOperatorCallExpr`:

```cpp
if (isAssignmentToStdAny(Op)) {
    Expr* RHS = Op->getArg(1);
    QualType rhsType = RHS->getType();
    std::string typeName = rhsType.getCanonicalType().getAsString();
    setField(lhsPath, "_any_type", SymValue::String(typeName));
    setField(lhsPath, "_has_value", SymValue::Const(1));
}
```

#### 5.3.3 make_any

```cpp
auto a = std::make_any<std::vector<int>>(10, 42);
// _any_type = "std::vector<int>"
```

**Implementation**: Detect `std::make_any<T>` calls, extract T from template argument.

#### 5.3.4 emplace

```cpp
std::any a;
a.emplace<std::string>("hello");
// _any_type = "std::string"
```

**Implementation**: Similar to make_any, extract type from template argument of emplace.

#### 5.3.5 reset and Destruction

```cpp
a.reset();  // _any_type = "", _has_value = 0
```

**Implementation**: Method annotation for reset() that clears both properties.

### 5.4 Validating std::any_cast

The key safety check: does the cast type match the stored type?

#### 5.4.1 Value Cast (Throws on Mismatch)

```cpp
std::any a = 42;
auto x = std::any_cast<int>(a);     // OK: returns copy of int
auto y = std::any_cast<double>(a);  // THROWS: bad_any_cast
```

**Implementation** in `VisitCallExpr`:

```cpp
if (isStdAnyCast(Callee)) {
    // Get the template argument (requested type)
    QualType requestedType = getAnyCastTemplateArg(CE);
    std::string requestedTypeName = requestedType.getCanonicalType().getAsString();

    // Get the any argument
    Expr* anyArg = CE->getArg(0);
    auto anyPath = getPath(anyArg);

    // Get current stored type
    SymValue storedType = getField(anyPath, "_any_type");

    if (storedType.isString()) {
        std::string storedTypeName = storedType.getStringValue();
        if (!storedTypeName.empty() && storedTypeName != requestedTypeName) {
            // PROVEN MISMATCH - report error
            reportError("any_cast to '%0' but any contains '%1'",
                        requestedTypeName, storedTypeName);
        }
    }
    // If storedType is unknown, allow (conservative)
}
```

#### 5.4.2 Reference Cast (Throws on Mismatch)

```cpp
std::any a = std::vector<int>{1, 2, 3};
auto& v = std::any_cast<std::vector<int>&>(a);  // Reference to internal
v.push_back(4);  // Modifies the vector inside a

a = 42;  // a reassigned
v.push_back(5);  // UNDEFINED BEHAVIOR - v is dangling
```

**Implementation**: Same type check, PLUS track reference dependency:

```cpp
if (requestedType->isReferenceType()) {
    // The returned reference depends on 'a'
    setField(resultPath, "_depends_on_any", anyPath.toString());
    // If 'a' is reassigned, 'result' becomes invalid
}
```

#### 5.4.3 Pointer Cast (Returns nullptr on Mismatch)

```cpp
std::any a = 42;
auto* p = std::any_cast<int>(&a);      // p != nullptr
auto* q = std::any_cast<double>(&a);   // q == nullptr
```

**Implementation**:

```cpp
if (isPointerFormAnyCast(CE)) {
    // Return type is T*
    // Set _nonnull based on type match
    SymValue storedType = getField(anyPath, "_any_type");
    if (storedType.isString()) {
        if (storedType.getStringValue() == requestedTypeName) {
            setField(resultPath, "_nonnull", SymValue::Const(1));
        } else if (!storedType.getStringValue().empty()) {
            setField(resultPath, "_nonnull", SymValue::Const(0));  // Proven null
        }
    }
}
```

### 5.5 Type Guards: Extracting Proofs from Conditionals

#### 5.5.1 Guard from a.type() == typeid(T)

```cpp
if (a.type() == typeid(int)) {
    // PROOF: a._any_type == "int"
    auto i = std::any_cast<int>(a);  // Safe - proven match
}
```

**Implementation** in guard extraction:

```cpp
// Detect pattern: anyVar.type() == typeid(SomeType)
if (auto* BO = dyn_cast<BinaryOperator>(Cond)) {
    if (BO->getOpcode() == BO_EQ) {
        if (isAnyTypeCall(BO->getLHS()) && isTypeidExpr(BO->getRHS())) {
            auto anyPath = getAnyFromTypeCall(BO->getLHS());
            QualType guardType = getTypeFromTypeid(BO->getRHS());

            // In true branch: a._any_type == "guardType"
            addProof(anyPath, "_any_type", ProofOp::EQ, guardType.getAsString());
        }
    }
}
```

#### 5.5.2 Guard from a.has_value()

```cpp
if (a.has_value()) {
    // PROOF: a._has_value == 1
    // But _any_type is still unknown!
    auto x = std::any_cast<int>(a);  // May still throw if wrong type
}
```

**Note**: `has_value()` only guards emptiness, NOT type. Already implemented in DTT.

#### 5.5.3 Guard from Pointer Cast

```cpp
if (auto* p = std::any_cast<int>(&a)) {
    // PROOF: a._any_type == "int"
    // PROOF: p._nonnull == 1
    *p = 42;  // Safe
}
```

**Implementation**: In condition extraction, detect the `if (auto* p = std::any_cast<T>(&a))` pattern.

### 5.6 Type Normalization

Different compilers produce different type names:

| Type | libstdc++ | libc++ |
|------|-----------|--------|
| `std::string` | `std::__cxx11::basic_string<char>` | `std::__1::basic_string<char, ...>` |
| `std::vector<int>` | `std::vector<int, std::allocator<int>>` | `std::__1::vector<int, ...>` |

**Solution**: Normalize type names before comparison:

```cpp
std::string normalizeTypeName(const std::string& name) {
    std::string result = name;
    // Remove inline namespace markers
    result = std::regex_replace(result, std::regex("::__1::"), "::");
    result = std::regex_replace(result, std::regex("::__cxx11::"), "::");
    // Remove default allocator
    result = std::regex_replace(result, std::regex(", std::allocator<[^>]+>"), "");
    return result;
}
```

### 5.7 Handling Unknown Types

When the stored type is unknown (e.g., assigned in another function):

```cpp
void foo(std::any& a) {
    // a._any_type is UNKNOWN here
    auto x = std::any_cast<int>(a);  // Can't prove error, ALLOW
}
```

**Policy**: Conservative - if we can't prove a type mismatch, allow the cast.

### 5.8 Diagnostics

Error messages should be clear and actionable:

```
error: std::any_cast<std::string> will throw bad_any_cast
       any variable 'a' contains 'int' (assigned at line 5)
       cast requested 'std::string' (at line 10)
note: consider using std::any_cast<std::string*>(&a) for safe checking
```

### 5.9 Complete any_cast Implementation

```cpp
void handleAnyCast(CallExpr* CE) {
    // 1. Get requested type from template argument
    QualType requestedType = getAnyCastTemplateArg(CE);
    if (requestedType.isNull()) return;

    std::string requested = normalizeTypeName(
        requestedType.getNonReferenceType().getCanonicalType().getAsString());

    // 2. Get the any argument
    Expr* anyArg = CE->getArg(0);
    bool isPointerForm = anyArg->getType()->isPointerType();

    // 3. Get path to the any variable
    Expr* anyExpr = isPointerForm ?
        cast<UnaryOperator>(anyArg)->getSubExpr() : anyArg;
    auto anyPath = getPath(anyExpr);

    // 4. Get stored type
    SymValue storedTypeVal = getField(anyPath, "_any_type");

    // 5. Check for type mismatch
    if (storedTypeVal.isString()) {
        std::string stored = storedTypeVal.getStringValue();
        if (!stored.empty() && stored != requested) {
            if (isPointerForm) {
                // Pointer form returns nullptr - track nullability
                setField(resultPath, "_nonnull", SymValue::Const(0));
            } else {
                // Value/reference form throws - report error
                reportAnyCastError(CE, requested, stored, anyPath);
            }
        } else if (stored == requested) {
            if (isPointerForm) {
                setField(resultPath, "_nonnull", SymValue::Const(1));
            }
            // Track reference dependency for reference form
            if (requestedType->isReferenceType()) {
                trackReferenceDependency(resultPath, anyPath);
            }
        }
    }
    // If unknown, allow (conservative)
}
```

### 5.10 std::any Method Annotations

```cpp
// In DTTStdLib.cpp
MethodAnnotations["std::any"] = {
    // Default constructor: empty
    {"any", 0, {}, {{"_has_value", "0"}, {"_any_type", "\"\""}}},

    // Copy constructor: copy type
    {"any", 1, {}, {{"_has_value", "$0._has_value"}, {"_any_type", "$0._any_type"}}},

    // reset(): clear
    {"reset", 0, {}, {{"_has_value", "0"}, {"_any_type", "\"\""}}},

    // has_value(): query (no state change)
    {"has_value", 0, {}, {}, {{"_has_value", ">", "0"}}},  // returns _has_value > 0

    // swap(): exchange types
    // (complex - need to swap _any_type between two variables)
};
```

### 5.11 Edge Cases

#### Nested any
```cpp
std::any a = std::any{42};  // any containing any containing int
// Track outer: _any_type = "std::any"
// Inner type is opaque
```

#### any Containing Variant
```cpp
std::any a = std::variant<int, std::string>{42};
// _any_type = "std::variant<int, std::string>"
// Variant's internal index is NOT tracked through any
```

#### Const Correctness
```cpp
const std::any a = 42;
// a._any_type = "int" (still tracked)
auto x = std::any_cast<int>(a);  // OK
```

### 5.12 Test Cases for std::any

```cpp
// 90_any_type_pass.cpp - Safe patterns
std::any a = 42;
auto x = std::any_cast<int>(a);  // OK

if (a.type() == typeid(int)) {
    auto y = std::any_cast<int>(a);  // OK - guarded
}

if (auto* p = std::any_cast<double>(&a)) {
    *p = 3.14;  // OK - null check
}

// 91_any_type_fail.cpp - Unsafe patterns
std::any a = 42;
auto s = std::any_cast<std::string>(a);  // ERROR: type mismatch

// 92_any_ref_fail.cpp - Reference invalidation
std::any a = std::vector<int>{1, 2, 3};
auto& v = std::any_cast<std::vector<int>&>(a);
a = 42;  // Reassign
v.push_back(4);  // ERROR: v is dangling
```

---

## Phase 6: Inner Value Tracking (Advanced)

**Goal**: Track when iterators, pointers, or other "dangerous" values are stored inside variant/any

### 6.1 The Problem: Tracked Values Inside Type-Erased Containers

```cpp
std::vector<int> data = {1, 2, 3};
auto it = data.begin();  // DTT tracks: it depends on data

// Store iterator in variant - does tracking propagate?
std::variant<int, std::vector<int>::iterator> v = it;

data.clear();  // Invalidates iterator

// Extract and use - should DTT catch this?
auto it2 = std::get<1>(v);
*it2;  // USE-AFTER-INVALIDATION
```

### 6.2 Approach: Propagate Inner Dependencies

When storing a tracked value in variant/any:

```cpp
std::variant<int, Iterator> v = it;
// v._variant_index = 1
// v._inner_depends_on = "data"  // Propagate iterator's dependency
// v._inner_path = "data.begin()"
```

When extracting:
```cpp
auto it2 = std::get<1>(v);
// it2 inherits dependency from v
// it2._depends_on = "data"
```

### 6.3 What Gets Propagated

| Stored Type | Properties to Propagate |
|-------------|------------------------|
| `iterator` | `_depends_on` (container) |
| `T*` | `_depends_on` (pointee lifetime) |
| `span<T>` | `_depends_on` (buffer) |
| `string_view` | `_depends_on` (string) |
| `reference_wrapper<T>` | `_depends_on` (referenced object) |
| `FILE*` | `_open` state |

### 6.4 Implementation Strategy

#### On Storage

```cpp
// In variant/any assignment handling
if (isTrackedType(rhsType)) {
    // Copy inner tracking to variant/any
    auto innerPath = getPath(RHS);
    auto innerDeps = getField(innerPath, "_depends_on");
    if (!innerDeps.isUnknown()) {
        setField(variantPath, "_inner_depends_on", innerDeps);
        setField(variantPath, "_inner_path", innerPath.toString());
    }
}
```

#### On Extraction

```cpp
// In std::get handling
if (isTrackedType(extractedType)) {
    // Restore inner tracking to extracted value
    auto innerDeps = getField(variantPath, "_inner_depends_on");
    if (!innerDeps.isUnknown()) {
        setField(resultPath, "_depends_on", innerDeps);
    }
}
```

### 6.5 Limitations of Inner Value Tracking

| Limitation | Reason | Impact |
|------------|--------|--------|
| **Only direct storage** | Can't track `variant<vector<iterator>>` | Limited depth |
| **Lost on copy** | `variant v2 = v1` - inner deps may not copy correctly | Conservative |
| **No cross-function** | Inner tracking lost at function boundaries | Need annotations |
| **Single dependency** | Can't track multiple dependencies | First one wins |

### 6.6 Priority

Inner value tracking is **Phase 6** (last) because:
1. It's complex to implement correctly
2. The primary use cases (wrong type access, reference invalidation) don't require it
3. Storing iterators in variant/any is relatively rare
4. Can be added incrementally after core features work

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

## Architecture: Why Hardcoded, Not Annotation-Based

### The Problem

DTT's annotation system (`DTTStdLib.cpp`) is designed for **integer-based property tracking**:

```cpp
// Works: integer properties
addPostConst("vector", "push_back", "_size", "old(_size) + 1");
addPre("vector", "front", "_size", GT, 0);
```

But variant/any tracking requires **type-based operations** that annotations cannot express:

| Operation | What's Needed | Annotation Support? |
|-----------|---------------|---------------------|
| Infer index from constructor arg type | Match `42` → `int` → index 0 in `variant<int, string>` | ❌ No |
| Extract template argument from `get<T>` | Parse `T` from call site | ❌ No |
| Store QualType for any | Track actual C++ type, not integer | ❌ No |
| Compare types for any_cast | `Ctx.hasSameType(stored, requested)` | ❌ No |

### Current Implementation

The variant/any tracking is **hardcoded in DTTPlugin.cpp** (~600 lines):

```cpp
// Hardcoded class name checks
if (className == "variant" && ...) { /* variant-specific logic */ }
if (className == "any" && ...) { /* any-specific logic */ }

// Hardcoded function name checks
if (fn == "get" && isInStdNamespace(...)) { /* std::get logic */ }
if (fn == "any_cast" && isInStdNamespace(...)) { /* any_cast logic */ }
```

**Pros:**
- Works correctly for std::variant and std::any
- Handles all stdlib implementations (libstdc++, libc++, MSVC STL)
- No internal implementation assumptions (only uses standard-defined API)

**Cons:**
- Users cannot annotate their own sum types (e.g., `Result<T, E>`, `MyVariant<...>`)
- Each new sum type requires core DTT changes
- Not extensible via the annotation mechanism

### Gap: User-Defined Sum Types Are Not Supported

Users who define their own discriminated unions or type-erased containers cannot get variant-like tracking:

```cpp
// User-defined sum type - NO DTT SUPPORT
template<typename T, typename E>
class Result {
    std::variant<T, E> storage_;  // Internal variant not tracked
public:
    bool is_ok() const;
    T& value();      // No precondition checking
    E& error();      // No precondition checking
};

// User-defined type-erased container - NO DTT SUPPORT
class AnyValue {
    void* data_;
    std::type_info* type_;
public:
    template<typename T> T& get();  // No type checking
};
```

### Future: What Generic Sum Type Support Would Require

To make this user-extensible, DTT would need new annotation primitives:

```cpp
// 1. Mark a class as a sum type with index property
[[clang::annotate("dtt:sum_type=_index")]]
template<typename... Ts>
class MyVariant { ... };

// 2. Mark accessor as requiring specific index
[[clang::annotate("dtt:sum_get=_index == $tparam0")]]  // $tparam0 = template arg
template<size_t I, typename... Ts>
auto& get(MyVariant<Ts...>& v);

// 3. Mark predicate as establishing index proof
[[clang::annotate("dtt:sum_holds=_index == $type_index($tparam0)")]]
template<typename T, typename... Ts>
bool holds(const MyVariant<Ts...>& v);

// 4. Mark type-erased accessor with type constraint
[[clang::annotate("dtt:type_cast=_stored_type == $tparam0")]]
template<typename T>
T& any_get(MyAny& a);
```

This would require:
1. **Type parameter references** in annotations (`$tparam0`, `$type_index(T)`)
2. **QualType storage** for tracking contained types
3. **Type comparison operators** in constraint expressions
4. **Template argument extraction** from call sites

This is a significant language extension beyond the current integer-based annotation system.

---

## Future Extensions

- `std::expected<T, E>` tracking (C++23)
- `boost::variant` support
- `std::variant` with `std::monostate`
- Recursive variant types
- Variant inside containers (`vector<variant<...>>`)
- **Generic sum type annotations** (see Architecture section above)
