# C++26 Contracts & Concepts Integration for DTT

**Version:** 0.1 Draft
**Status:** Proposal
**Date:** 2026-02-18

## Abstract

This proposal describes how DTT can leverage two C++ language features — **C++26 contracts** (P2900) and **C++20 concepts** — to provide a more natural, standards-aligned annotation surface. Instead of relying exclusively on `[[clang::annotate("dtt:...")]]` strings, DTT would additionally parse standard C++ contract specifiers (`pre`/`post`) and concept constraints, mapping recognized patterns to its existing proof system.

## 1. Motivation

### 1.1 The Annotation Tax

Today, DTT annotations look like this:

```cpp
[[clang::annotate("dtt:requires=_size > 0")]]
[[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
void push(int val);
```

This works, but has drawbacks:

- **Opaque strings** — no syntax highlighting, no IDE autocompletion, no compiler type-checking of the expression
- **DTT-specific vocabulary** — `_size`, `_nonnull`, `arg0` are DTT internals, not C++ identifiers
- **Non-portable** — the annotations only mean something to DTT; other tools ignore them
- **Duplication** — if a project adopts C++26 contracts for runtime checking, DTT annotations become a parallel, redundant specification

### 1.2 The Opportunity

C++26 contracts provide standard syntax for the same intent:

```cpp
void push(int val)
    pre(size() < capacity())       // standard C++26
    post(size() == old_size + 1);  // standard C++26
```

If DTT can read and interpret these, users get:

1. **Single source of truth** — one annotation serves both runtime contract checking and compile-time DTT verification
2. **Standard syntax** — IDEs, linters, and compilers all understand `pre`/`post`
3. **Gradual adoption** — existing `[[clang::annotate]]` annotations continue to work; contracts are an additional input channel
4. **Portability** — contracts work with any conforming C++26 compiler, with or without DTT

Similarly, C++20 concepts can express type-level constraints that DTT can interpret as property requirements on tracked types.

## 2. Design Overview

Three integration layers, each independent and incrementally adoptable:

| Layer | Feature | What DTT Does |
|-------|---------|---------------|
| **A** | C++26 `pre`/`post` with DTT marker predicates | Recognizes `dtt::nonempty(v)`, `dtt::nonnull(p)` etc. inside contract specifiers |
| **B** | C++26 `pre`/`post` with standard C++ expressions | Maps `.size()`, `.empty()`, `!= nullptr` to tracked properties |
| **C** | C++20 concepts as DTT property carriers | Recognizes concept constraints like `NonEmpty<C>` as implying `_size > 0` |

### Priority

Layer A is the simplest (exact name matching). Layer B is the most valuable (zero DTT vocabulary required). Layer C is the most experimental (concepts are type-level, DTT tracks values).

## 3. Layer A: DTT Marker Predicates in Contracts

### 3.1 Design

DTT provides a header `<dtt/contracts.h>` with `constexpr` predicate functions:

```cpp
namespace dtt {
    // Container predicates
    template<typename C> constexpr bool nonempty(const C&) { return true; }
    template<typename C> constexpr bool empty(const C&)    { return true; }
    template<typename C> constexpr bool sorted(const C&)   { return true; }
    template<typename C> constexpr bool has_space(const C&) { return true; }

    // Pointer predicates
    template<typename T> constexpr bool nonnull(const T*)  { return true; }
    template<typename T> constexpr bool nonnull(const std::unique_ptr<T>&) { return true; }
    template<typename T> constexpr bool nonnull(const std::shared_ptr<T>&) { return true; }

    // Optional/variant predicates
    template<typename T> constexpr bool has_value(const std::optional<T>&) { return true; }

    // Resource predicates
    constexpr bool is_open(FILE*)  { return true; }
    constexpr bool is_valid(int fd) { return true; }  // file descriptor
}
```

These functions **always return `true` at runtime** — they impose zero runtime cost. But DTT recognizes calls to `dtt::*` inside contract specifiers and maps them to tracked properties.

### 3.2 Usage

```cpp
#include <dtt/contracts.h>

int front(std::vector<int>& v)
    pre(dtt::nonempty(v))          // DTT reads: v._size > 0
{
    return v[0];                    // DTT proves safe
}

void process(int* p, int n)
    pre(dtt::nonnull(p))           // DTT reads: p._nonnull != 0
    pre(n > 0)                     // DTT reads: arg1 > 0 (Layer B)
{
    *p = 42;                        // DTT proves safe
}

FILE* open_config(const char* path)
    post(r : r == nullptr || dtt::is_open(r))  // DTT reads: result._nonnull==0 || result.@open==1
{
    return fopen(path, "r");
}
```

### 3.3 Implementation

In `DTTPlugin.cpp`, when visiting a `FunctionDecl`:

1. Check for `PreAttr` / `PostAttr` AST nodes (Clang's representation of C++26 contracts)
2. Walk the contract expression AST
3. If a `CallExpr` calls a function in `namespace dtt`, map the function name to the corresponding DTT property via a lookup table (reusing `ConceptRegistry`)
4. Extract the argument to determine which tracked variable the property applies to
5. Feed the resulting constraint into the existing `ConstraintExprParser` / proof system

```
dtt::nonempty(v)  →  parse "v._size > 0"  →  ConstraintExpr(CmpGT, FieldRef("_size"), Literal(0))
dtt::nonnull(p)   →  parse "p._nonnull != 0"  →  ConstraintExpr(CmpNE, FieldRef("_nonnull"), Literal(0))
```

### 3.4 Mapping Table

| DTT Predicate | Tracked Property | Constraint |
|---------------|-----------------|------------|
| `dtt::nonempty(c)` | `c._size` | `_size > 0` |
| `dtt::empty(c)` | `c._size` | `_size == 0` |
| `dtt::sorted(c)` | `c._sorted` | `_sorted == 1` |
| `dtt::has_space(c)` | `c._size`, `c._capacity` | `_size < _capacity` |
| `dtt::nonnull(p)` | `p._nonnull` | `_nonnull != 0` |
| `dtt::has_value(o)` | `o._has_value` | `_has_value != 0` |
| `dtt::is_open(f)` | `f.@open` | `@open == 1` |
| `dtt::is_valid(fd)` | `fd.@fd` | `@fd == 1` |

## 4. Layer B: Standard C++ Expressions in Contracts

### 4.1 Design

DTT recognizes standard C++ method calls and operators inside `pre`/`post` and maps them to tracked properties:

```cpp
int front(std::vector<int>& v)
    pre(!v.empty())                // DTT infers: v._size > 0
    pre(v.size() > 0)             // DTT infers: v._size > 0 (equivalent)
{
    return v[0];
}

int safe_access(std::vector<int>& v, int idx)
    pre(idx >= 0)
    pre(idx < v.size())           // DTT infers: arg1 < v._size
{
    return v[idx];
}
```

### 4.2 Expression Recognition Rules

DTT walks the contract `Expr*` AST and applies pattern matching:

| C++ Expression Pattern | DTT Interpretation |
|------------------------|-------------------|
| `c.size()` | `c._size` |
| `c.empty()` | `c._size == 0` |
| `!c.empty()` | `c._size > 0` |
| `c.capacity()` | `c._capacity` |
| `o.has_value()` | `o._has_value != 0` |
| `o.value()` | requires `o._has_value != 0` |
| `p != nullptr` | `p._nonnull != 0` |
| `p == nullptr` | `p._nonnull == 0` |
| `std::holds_alternative<T>(v)` | `v._variant_index == N` (where N is T's index) |
| `v.data() != nullptr` | `v._nonnull != 0` |

### 4.3 Method-to-Property Resolution

The mapping reuses DTTStdLib's existing annotations. When DTT encounters `v.size()` in a contract expression, it looks up the `returns` field for `{"vector", "size", 0}` in `MethodAnnotations`, which yields `"_size"`. This means the same source of truth that governs runtime tracking also governs contract interpretation.

```
v.size()  →  MethodAnnotations[{"vector", "size", 0}].returns == "_size"
          →  substitute: v._size
```

### 4.4 Arithmetic and Comparison Passthrough

Arithmetic and comparison operators in contract expressions map directly:

```cpp
pre(v.size() >= 2)          // v._size >= 2
pre(idx < v.size() - 1)     // arg < v._size - 1
post(r : r.size() == v.size() + w.size())  // result._size == v._size + w._size
```

### 4.5 `old()` Semantics

C++26 contracts don't have `old()` (unlike Eiffel or ACSL). DTT can provide it via the marker header:

```cpp
namespace dtt {
    template<typename T> const T& old(const T& x);  // never called; DTT interprets it
}
```

Usage:

```cpp
void push_back(std::vector<int>& v, int x)
    post(v.size() == dtt::old(v).size() + 1)   // DTT reads: v._size == old(v._size) + 1
{
    v.push_back(x);
}
```

### 4.6 Implementation Strategy

Processing a C++26 contract specifier:

```
pre(expr) / post(result_name : expr)
   │
   ▼
Walk Expr* AST (Clang Stmt/Expr tree)
   │
   ├── CXXMemberCallExpr on tracked type?
   │     ├── callee == "size"    → replace with _size
   │     ├── callee == "empty"   → replace with (_size == 0)
   │     ├── callee == "has_value" → replace with (_has_value != 0)
   │     └── look up MethodAnnotations.returns for the method
   │
   ├── CallExpr to dtt::* function?
   │     └── Layer A handling (marker predicates)
   │
   ├── BinaryOperator (&&, ||, <, >, ==, !=, +, -, etc.)?
   │     └── recurse into LHS/RHS
   │
   ├── UnaryOperator (!)?
   │     └── negate child
   │
   ├── DeclRefExpr to function parameter?
   │     └── map to argN or tracked variable path
   │
   └── IntegerLiteral / FloatingLiteral?
         └── map to Literal(value)
```

The output is a `ConstraintExprPtr` — the exact same AST that the string-based parser produces. From that point on, the entire proof system works unchanged.

## 5. Layer C: C++20 Concepts as Property Carriers

### 5.1 The Challenge

Concepts constrain types, not values. `NonEmpty` can say "this type has a `.size()` method" but can't say "this particular instance has `.size() > 0`." However, concepts can serve as **documentation** that DTT interprets as property requirements.

### 5.2 Design: DTT-Aware Concepts

DTT provides concepts that carry semantic meaning:

```cpp
// <dtt/concepts.h>
namespace dtt {
    // These concepts are trivially satisfied at the type level.
    // DTT interprets them as value-level requirements on the parameter.

    template<typename C>
    concept NonEmpty = requires(const C& c) {
        { c.size() } -> std::convertible_to<std::size_t>;
    };

    template<typename C>
    concept Sorted = requires(const C& c) {
        { c.size() } -> std::convertible_to<std::size_t>;
        { c.begin() } -> std::input_iterator;
    };

    template<typename T>
    concept NonNull = std::is_pointer_v<T>
        || requires(const T& t) { { t.get() } -> std::convertible_to<typename T::element_type*>; };

    template<typename T>
    concept HasValue = requires(const T& t) {
        { t.has_value() } -> std::same_as<bool>;
        { *t };
    };
}
```

### 5.3 Usage

```cpp
#include <dtt/concepts.h>

// Abbreviated function template with concept constraint
int front(dtt::NonEmpty auto& v) {
    return v[0];     // DTT infers: v._size > 0 from dtt::NonEmpty constraint
}

// Template with requires clause
template<typename C>
    requires dtt::Sorted<C> && dtt::NonEmpty<C>
int binary_search(const C& c, int target) {
    // DTT infers: c._size > 0 && c._sorted == 1
    return /* ... */;
}

// Concept in trailing requires
template<typename T>
void process(T* p, int n)
    requires dtt::NonNull<T*>      // DTT reads: p._nonnull != 0
{
    *p = 42;
}
```

### 5.4 Concept-to-Property Mapping

| DTT Concept | Implies | Tracked Property |
|-------------|---------|-----------------|
| `dtt::NonEmpty<C>` | Container is non-empty | `_size > 0` |
| `dtt::Sorted<C>` | Container is sorted | `_sorted == 1` |
| `dtt::NonNull<T>` | Pointer is non-null | `_nonnull != 0` |
| `dtt::HasValue<T>` | Optional-like has value | `_has_value != 0` |

### 5.5 Implementation

When DTT encounters a `FunctionTemplateDecl` or `ConceptSpecializationExpr`:

1. Walk the `requires` clause / concept constraints
2. For each constraint, check if it references a concept in namespace `dtt`
3. Map the concept name to a DTT property requirement
4. Determine which parameter the concept constrains (from the template parameter → function parameter binding)
5. Inject the property as a precondition, same as `dtt:requires=`

### 5.6 Limitations

- Concepts are **type-level** — `dtt::NonEmpty<std::vector<int>>` is satisfied for ALL vectors, not just non-empty ones. This is a semantic lie at the type level but a useful signal for DTT.
- Concept satisfaction is checked at instantiation, not at the call site. DTT would need to check the call site independently.
- Overload resolution based on concepts may interact unexpectedly with DTT's property requirements.

These limitations make Layer C the most experimental. It may be better to use concepts purely as documentation markers that DTT reads, rather than as actual C++ concept constraints. An alternative:

```cpp
// Concepts as pure markers (no overload resolution effect)
namespace dtt {
    template<typename C> concept NonEmpty = true;  // always satisfied
}

// DTT still interprets it
int front(dtt::NonEmpty auto& v) {
    return v[0];  // DTT requires _size > 0
}
```

The `= true` approach avoids overload resolution issues while still giving DTT a signal.

## 6. Combining All Three Layers

The layers compose naturally:

```cpp
#include <dtt/contracts.h>
#include <dtt/concepts.h>

// Layer C: concept constrains the type
template<dtt::NonEmpty Container>
// Layer B: standard C++ in contract
auto front(Container& c) -> typename Container::value_type
    pre(!c.empty())                  // redundant with concept, but explicit
    post(r : r == c.front())         // postcondition on return value
{
    return c.front();
}

// Layer A + B mixed
void safe_copy(const char* src, char* dst, int n)
    pre(dtt::nonnull(src))           // Layer A: marker predicate
    pre(dtt::nonnull(dst))           // Layer A: marker predicate
    pre(n > 0)                       // Layer B: standard C++ expression
{
    std::memcpy(dst, src, n);
}
```

## 7. Backward Compatibility & Migration Path

### 7.1 No Breaking Changes

All existing `[[clang::annotate("dtt:...")]]` annotations continue to work unchanged. Contracts and concepts are an **additional** input channel, not a replacement.

### 7.2 Precedence

If both annotation styles are present on the same function, DTT collects preconditions/postconditions from all sources:

```cpp
[[clang::annotate("dtt:requires=_size > 0")]]    // Old style
int front(std::vector<int>& v)
    pre(!v.empty())                                // New style (C++26)
{
    return v[0];
}
```

Both preconditions are collected. Since they express the same constraint, this is harmless (the proof system handles redundant constraints).

### 7.3 Migration Tooling (Future)

A `dtt-migrate` tool could automatically convert:
```
[[clang::annotate("dtt:requires=_size > 0")]]  →  pre(!v.empty())
[[clang::annotate("dtt:ensures=_nonnull != 0")]]  →  post(r : r != nullptr)
```

## 8. Interaction with DTT's Existing Features

### 8.1 Contract Propagation

Existing contract propagation (injecting `requires` as assumptions inside the function body) extends naturally to C++26 contracts:

```cpp
int front(std::vector<int>& v)
    pre(!v.empty())       // DTT injects: assume v._size > 0 at function entry
{
    return v[0];           // Safe: v._size > 0 is known
}
```

### 8.2 Auto-Inference

DTT already auto-infers postconditions from method implementations. C++26 `post` specifiers would **augment** auto-inference — explicit `post` annotations take priority, and auto-inferred postconditions fill gaps.

### 8.3 Ghost Fields

Ghost fields like `@open`, `@allocated`, `@fd` have no C++ representation. They can only be expressed via Layer A (DTT marker predicates) or the existing annotation syntax:

```cpp
FILE* open_log()
    post(r : r == nullptr || dtt::is_open(r))    // Layer A
{
    return fopen("log.txt", "w");
}
```

Standard C++ contracts (Layer B) cannot express ghost fields since there's no `.is_open()` method on `FILE*`.

### 8.4 `old()` Values

C++26 contracts lack `old()`. DTT provides `dtt::old()` as a marker function (Layer A), or the existing annotation syntax continues to work:

```cpp
[[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]   // Old style: works
post(v.size() == dtt::old(v).size() + 1)                     // New style: works via marker
```

## 9. Implementation Phases

### Phase 1: Layer A (Marker Predicates)

- Create `include/dtt/contracts.h` with marker predicate functions
- Add AST visitor for `PreAttr`/`PostAttr` in DTTPlugin.cpp
- Pattern-match `CallExpr` to `dtt::*` functions
- Map to existing `ConstraintExprPtr` system
- Add tests: `examples/2001_contract_marker_pass.cpp`, `*_fail.cpp`

### Phase 2: Layer B (Standard C++ Expression Mapping)

- Build `Expr*` → `ConstraintExprPtr` translator
- Handle `CXXMemberCallExpr` (`.size()`, `.empty()`, `.has_value()`)
- Handle `BinaryOperator`, `UnaryOperator`, `DeclRefExpr`
- Use `MethodAnnotations.returns` for method-to-property resolution
- Add tests: `examples/2010_contract_stdexpr_pass.cpp`, `*_fail.cpp`

### Phase 3: Layer C (Concept Constraints)

- Create `include/dtt/concepts.h` with DTT-aware concepts
- Add visitor for `ConceptSpecializationExpr` / `RequiresExpr`
- Map concept names to DTT properties
- Handle abbreviated function templates (`dtt::NonEmpty auto& v`)
- Add tests: `examples/2020_concept_constraint_pass.cpp`, `*_fail.cpp`

### Phase 4: Integration & Migration

- Test all three layers in combination
- Document migration paths
- Consider `dtt-migrate` tool for automated conversion

## 10. Compiler Requirements

| Feature | Required Compiler |
|---------|------------------|
| Layer A, B (C++26 contracts) | Clang with P2900 support (experimental fork available; mainline TBD) |
| Layer C (C++20 concepts) | Clang 16+ (concepts fully supported) |
| Existing annotations | Clang 14+ (no change) |

**Important:** Until C++26 contracts land in mainline Clang, Layers A and B require users to use an experimental Clang fork. However, the design ensures no regressions — existing annotation-based users are unaffected.

Layer C (concepts) can be implemented immediately since C++20 concepts are already in mainline Clang 18.

## 11. Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| Clang contract AST representation changes before release | Abstract the `PreAttr`/`PostAttr` visitor behind an interface; update when AST stabilizes |
| Users mix annotation styles inconsistently | DTT collects from all sources; document best practices |
| Concept `= true` trick confuses IDEs/linters | Provide concepts with real structural constraints where possible; `= true` as fallback |
| `old()` marker function breaks if actually called at runtime | Mark as `[[deprecated("DTT compile-time only")]]` or `= delete` with explanation |
| Layer B expression mapping misidentifies user methods as STL methods | Only apply Layer B to types registered in DTTStdLib/DTTCLib |

## 12. Open Questions

1. **Should DTT warn when contract and annotation contradict?** E.g., annotation says `_size > 0` but contract says `v.size() >= 0` (weaker).
2. **Should `contract_assert()` (in-body assertion) be treated as a proof hint?** DTT could use in-body contract assertions to strengthen its proof context, similar to how it uses `if (!v.empty())` guards today.
3. **Should DTT validate that contract predicates match tracked properties?** E.g., warn if `pre(v.size() > 0)` is used on a type DTT doesn't track.
4. **P2900 `ignore` semantic**: When contracts are compiled with `ignore` semantic, they are parsed but not evaluated. DTT operates at the AST level, so DTT verification works regardless of the runtime evaluation semantic — this is a feature, not a bug.
5. **Virtual functions**: P2900 deferred contracts on virtual functions. How does this affect DTT's handling of virtual dispatch? (Currently DTT doesn't track through virtual calls.)

## 13. Related Work

| System | Contracts + Static Analysis |
|--------|---------------------------|
| **Frama-C** (C) | ACSL annotations (`/*@ requires ... */`) parsed by static analyzers; similar dual role to what we propose |
| **CodeContracts** (.NET) | `Contract.Requires()` / `Contract.Ensures()` used by both runtime and static checker |
| **SPARK Ada** | Contracts (`Pre`, `Post` aspects) proven statically by GNATprove |
| **Dafny** | `requires` / `ensures` verified by Boogie/Z3 at compile time |
| **Spec#** | C# with contracts verified by Boogie |

DTT's approach is most similar to Frama-C's: the same annotation syntax serves both runtime checking and static verification, with the static tool extracting deeper meaning than the runtime evaluator.
