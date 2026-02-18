# Automatic Constraint Inference from C++26 Contracts & C++20 Concepts

**Version:** 0.3
**Status:** Phases 1-2 implemented (dtt_contract_assert); Phases 3-4 pending
**Date:** 2026-02-18

## Abstract

DTT already auto-infers postconditions from method implementations and tracks properties through STL operations. This proposal extends that same automatic inference philosophy to two standard C++ features: **C++26 contract specifiers** (`pre`/`post`) and **C++20 concept constraints**. When DTT encounters these constructs, it extracts the same constraints it would from its `[[clang::annotate("dtt:...")]]` annotations — automatically, from standard C++ code, with zero DTT-specific vocabulary.

No new namespaces. No marker predicates. No special headers. Just deeper inference from code the programmer was already writing.

## 1. Motivation

### 1.1 The Inference Gap

DTT already infers a lot automatically:

```cpp
std::vector<int> v;
v.push_back(42);     // DTT infers: v._size = 1
v.front();           // DTT proves safe: _size > 0 ✓
```

But function boundaries break this. DTT needs explicit annotations to know what a function requires and guarantees:

```cpp
[[clang::annotate("dtt:requires=_size > 0")]]
int get_first(std::vector<int>& v) { return v.front(); }
```

C++26 contracts express the same information in standard C++:

```cpp
int get_first(std::vector<int>& v)
    pre(!v.empty())
{ return v.front(); }
```

DTT should understand this automatically. The programmer already wrote the constraint — in a form the compiler parses, type-checks, and the IDE understands. DTT should harvest it.

### 1.2 The Concept Dimension

C++20 concepts constrain types, but their structural requirements reveal what operations a type supports — and therefore what properties DTT can track:

```cpp
template<std::ranges::sized_range R>
auto first(R& r) { return *r.begin(); }
```

The `sized_range` constraint tells DTT: this type has `.size()` → DTT can track `_size`. Combined with a contract:

```cpp
template<std::ranges::sized_range R>
auto first(R& r)
    pre(!r.empty())           // DTT infers: r._size > 0
{ return *r.begin(); }
```

The concept tells DTT *what to track*. The contract tells DTT *what value to require*. Together, they give DTT everything it needs — from standard C++ alone.

## 2. Core Principle

> **DTT infers constraints from standard C++ constructs the same way it infers them from its own annotations. No special vocabulary. No opt-in. Automatic.**

When DTT sees `pre(!v.empty())`, it produces exactly the same internal `ConstraintExpr` as when it parses `[[clang::annotate("dtt:requires=_size > 0")]]`. The existing proof system, Z3 integration, contract propagation, and diagnostic categories all work unchanged downstream.

## 3. Contract Inference (C++26 `pre`/`post`/`contract_assert`)

### 3.1 How It Works

DTT visits `pre`/`post` AST nodes on `FunctionDecl`s and translates the contract expression (`Expr*`) into its internal `ConstraintExprPtr`. This is an `Expr*`→`ConstraintExprPtr` translator that walks the Clang AST.

```
pre(!v.empty())
 │
 └─ UnaryOperator '!'
     └─ CXXMemberCallExpr: vector<int>::empty()
         └─ MemberExpr: v.empty
             └─ DeclRefExpr: v (ParmVarDecl, param index 0)

DTT resolves:
  vector::empty() → returns "_size == 0" (from MethodAnnotations, via boolPostconds)
  !(_size == 0)   → _size > 0
  v is param 0    → constraint on arg0

Result: ConstraintExpr(CmpGT, FieldRef("_size"), Literal(0))
        applied to parameter 0
```

This is identical to what `[[clang::annotate("dtt:requires=_size > 0")]]` produces.

### 3.2 Expression Mapping

DTT resolves method calls in contract expressions using the same `MethodAnnotations` table that already powers runtime tracking. No new tables or mappings — the same source of truth:

| Expression in contract | MethodAnnotations lookup | DTT constraint |
|------------------------|-------------------------|---------------|
| `v.size()` | `{"vector","size",0}.returns = "_size"` | `_size` (as value) |
| `v.empty()` | `{"vector","empty",0}.boolPostconds` | `_size == 0` (true branch) |
| `!v.empty()` | negation of above | `_size > 0` |
| `v.capacity()` | `{"vector","capacity",0}.returns = "_capacity"` | `_capacity` (as value) |
| `o.has_value()` | `{"optional","has_value",0}.returns` | `_has_value != 0` |
| `p != nullptr` | pointer comparison pattern | `_nonnull != 0` |
| `p == nullptr` | pointer comparison pattern | `_nonnull == 0` |
| `std::holds_alternative<T>(v)` | variant index resolution | `_variant_index == N` |

For arithmetic, comparison, and logical operators (`+`, `-`, `<`, `>=`, `&&`, `||`, `!`), DTT maps directly to the corresponding `ConstraintExpr` node kinds (`Add`, `Sub`, `CmpLT`, `CmpGE`, `LogicalAnd`, `LogicalOr`, `LogicalNot`).

For `DeclRefExpr` referencing function parameters, DTT maps to `ArgRef(N)` by parameter index.

For integer/boolean literals, DTT maps to `Literal(value)`.

### 3.3 Postcondition Inference

`post` specifiers work the same way:

```cpp
void add(std::vector<int>& v, int x)
    post(v.size() > 0)                    // DTT ensures: v._size > 0
    post(!v.empty())                      // equivalent, also inferred
{ v.push_back(x); }
```

The named return value in `post(r : expr)` maps to DTT's `ResultRef`:

```cpp
std::optional<int> find(const std::vector<int>& v, int x)
    post(r : !r.has_value() || r.value() >= 0)
{ /* ... */ }
```

### 3.4 `contract_assert` as Proof Hint

DTT already uses `if`-guards as proof context (e.g., `if (!v.empty()) { v.front(); }`). The `contract_assert` statement is a stronger signal — an explicit assertion the programmer believes holds at that program point:

```cpp
void process(std::vector<int>& v, int i) {
    // ... complex logic that establishes i < v.size() ...
    contract_assert(i < v.size());     // DTT ingests as proof fact
    v[i] = 42;                          // DTT proves safe
}
```

DTT treats `contract_assert(expr)` identically to an `if (expr)` guard — it adds the translated constraint to the proof context for all subsequent statements in the same scope. This is sound: if the assertion is false at runtime, the contract violation handler fires (or the program terminates, depending on the evaluation semantic). DTT's static analysis can assume the assertion holds.

### 3.5 Contract Propagation (Unchanged)

Existing contract propagation works identically. A `pre` constraint inferred from a contract specifier is injected as an assumption inside the function body, just like `dtt:requires=` is today:

```cpp
int front(std::vector<int>& v)
    pre(!v.empty())           // DTT injects: assume v._size > 0 at entry
{
    return v.front();          // Safe: _size > 0 known
}

int main() {
    std::vector<int> v;
    front(v);                  // DTT ERROR (Category F): v._size == 0, requires > 0
    v.push_back(1);
    front(v);                  // DTT SAFE (Category E): v._size == 1 > 0 ✓
}
```

### 3.6 Translation Pipeline

```
C++26 contract specifier (Clang AST)
   │
   ▼
Expr* → ConstraintExprPtr translator (NEW)
   │
   ├── CXXMemberCallExpr → look up MethodAnnotations[{class,method,arity}]
   │     ├── .returns field? → substitute property name (e.g., _size)
   │     ├── .boolPostconds? → expand to equality constraint
   │     └── unrecognized method? → skip (no inference for unknown methods)
   │
   ├── BinaryOperator → map to CmpGT/CmpLT/CmpEQ/Add/Sub/etc.
   │     └── recurse into LHS, RHS
   │
   ├── UnaryOperator '!' → LogicalNot, recurse into operand
   │
   ├── DeclRefExpr (parameter) → ArgRef(param_index)
   │
   ├── IntegerLiteral → Literal(value)
   │
   ├── CXXNullPtrLiteralExpr → Literal(0) for pointer comparisons
   │
   └── CallExpr (free function)
         └── look up FreeFunctionAnnotations[{name,arity}]

   │
   ▼
ConstraintExprPtr (existing type)
   │
   ▼
Existing proof system (Z3, tracking, diagnostics) — UNCHANGED
```

The *only* new code is the `Expr*` → `ConstraintExprPtr` translator. Everything downstream is reused.

## 4. Concept Inference (C++20 `concept` / `requires`)

### 4.1 How It Works

Concepts tell DTT what *structural capabilities* a type has. DTT uses this to decide what properties to auto-track — and when combined with contracts, what constraints to enforce.

There are two inference modes:

#### Mode 1: Standard Concepts → Trackable Properties

When a template parameter is constrained by a standard concept, DTT knows what the type supports:

| Standard Concept | DTT Infers |
|-----------------|------------|
| `std::ranges::sized_range<R>` | R has `.size()` → track `_size` |
| `std::ranges::contiguous_range<R>` | R has `.data()` and `.size()` → track `_size`, `_nonnull` for `.data()` |
| `std::ranges::random_access_range<R>` | R has `operator[]` → bounds checking applicable |
| `std::ranges::input_range<R>` | R has `.begin()`/`.end()` → iterable |
| `std::convertible_to<T, bool>` | T is boolean-testable → truthiness tracking |
| `std::integral<T>` | T is numeric → arithmetic constraints apply |

This doesn't add constraints on its own — it tells DTT *what vocabulary of properties* is available for a generic parameter. Constraints come from contracts or code analysis.

#### Mode 2: Concept Bodies → Inferred Constraints

DTT can look inside a concept definition to extract structural requirements:

```cpp
template<typename C>
concept Searchable = requires(const C& c, typename C::value_type v) {
    { c.size() } -> std::convertible_to<std::size_t>;
    { c.empty() } -> std::same_as<bool>;
    { c[0] } -> std::same_as<typename C::const_reference>;
    requires std::totally_ordered<typename C::value_type>;
};
```

From this, DTT infers:
- `c.size()` exists → `_size` is trackable
- `c.empty()` exists → can use `_size == 0` / `_size > 0`
- `c[0]` exists → bounds checking via `_size` is applicable
- `std::totally_ordered` → type supports ordering → `_sorted` is meaningful

Now when this concept is used:

```cpp
template<Searchable C>
auto binary_search(const C& c, typename C::value_type target)
    pre(!c.empty())
    pre(std::is_sorted(c.begin(), c.end()))  // DTT can interpret if it tracks sorted
{
    // DTT knows: c._size > 0, and c is sorted
}
```

The concept told DTT *what's trackable*; the contract told DTT *what to require*.

### 4.2 Concept-Informed Tracking for Generic Code

Today, DTT's type tracking is keyed on class names (`"vector"`, `"optional"`, etc.) in `DTTStdLib.cpp`. This doesn't work for generic code operating on abstract types:

```cpp
template<typename Container>
auto first(Container& c) { return c.front(); }
```

Without knowing what `Container` is, DTT can't check the `front()` precondition. But if the template is constrained:

```cpp
template<std::ranges::sized_range Container>
auto first(Container& c)
    pre(c.size() > 0)
{ return *c.begin(); }
```

Now DTT knows:
1. `Container` satisfies `sized_range` → `.size()` exists → `_size` is trackable
2. `pre(c.size() > 0)` → `c._size > 0` is required

At the call site:

```cpp
std::vector<int> v = {1, 2, 3};
first(v);    // DTT: v._size == 3 > 0 ✓

std::list<int> l;
first(l);    // DTT: l._size == 0, requires > 0 → ERROR
```

DTT can verify the call site against the contract, even though the function is generic, because the concept told DTT what properties the generic type supports.

### 4.3 What Concepts Cannot Do

Concepts are type-level. They cannot express:

- **Specific values**: "this vector is non-empty" (that's a contract, not a concept)
- **Ghost state**: `@open`, `@allocated` (no C++ syntax for these)
- **Temporal properties**: "was sorted before the last push_back" (that's flow-sensitive tracking)

Concepts feed DTT's *property discovery*. Contracts and code analysis feed DTT's *proof system*. They are complementary: concepts expand what DTT can track on generic types; contracts and flow analysis establish what values those properties hold.

## 5. Combined Example

No DTT annotations. No special headers. Just standard C++:

```cpp
#include <vector>
#include <ranges>
#include <algorithm>
#include <optional>

// Concept tells DTT: C has .size(), .empty(), operator[]
// Contract tells DTT: c must be non-empty, idx must be in bounds
template<std::ranges::random_access_range C>
auto safe_at(const C& c, std::size_t idx) -> std::ranges::range_value_t<C>
    pre(!c.empty())
    pre(idx < c.size())
{
    return c[idx];
}

// Contract tells DTT: result, if present, came from v
template<std::ranges::forward_range R>
auto find_positive(const R& r) -> std::optional<std::ranges::range_value_t<R>>
    post(result : !result.has_value() || result.value() > 0)
{
    for (auto&& x : r)
        if (x > 0) return x;
    return std::nullopt;
}

int main() {
    std::vector<int> v;

    // safe_at(v, 0);          // DTT ERROR: v._size == 0, pre requires > 0

    v.push_back(42);
    safe_at(v, 0);             // DTT SAFE: v._size == 1 > 0, 0 < 1 ✓

    // safe_at(v, 5);          // DTT ERROR: v._size == 1, 5 < 1 is false

    auto result = find_positive(v);
    if (result.has_value()) {
        int x = result.value();           // DTT SAFE: guarded by has_value check
        contract_assert(x > 0);           // DTT SAFE: postcondition guarantees this
    }
}
```

DTT verifies all of this from standard C++ alone. The programmer wrote zero DTT-specific annotations.

## 6. Interaction with Existing DTT Features

### 6.1 Coexistence with `[[clang::annotate]]`

Both annotation styles feed the same proof system. If both are present, DTT collects constraints from all sources:

```cpp
[[clang::annotate("dtt:requires=_size > 0")]]
int front(std::vector<int>& v)
    pre(!v.empty())                        // Same constraint, different source
{ return v.front(); }
```

This is harmless — redundant constraints don't confuse Z3.

### 6.2 Ghost Fields Remain Annotation-Only

Ghost fields (`@open`, `@allocated`, `@fd`) have no C++ representation. There's no `.is_open()` on `FILE*`. These remain expressible only via `[[clang::annotate]]`:

```cpp
[[clang::annotate("dtt:ensures=result.@open == 1")]]
FILE* open_log();
```

This is fine. Ghost fields are DTT's domain — contracts handle the C++-expressible surface, DTT annotations handle the rest.

### 6.3 `old()` Values

C++26 contracts have no `old()` construct. Postconditions referencing pre-call state remain annotation-only:

```cpp
[[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
void push(std::vector<int>& v, int x);
```

A future C++ standard might add `old()` to contracts. If/when it does, DTT would automatically support it via the same `Expr*` translator.

### 6.4 Auto-Inference Priority

DTT auto-infers postconditions from method implementations. Explicit `post` specifiers are *stronger* — they override auto-inference when present. This matches how explicit `dtt:ensures=` annotations already override auto-inference.

## 7. Implementation

### 7.1 New Code: `Expr*` → `ConstraintExprPtr` Translator

A single new function in `DTTPlugin.cpp`:

```cpp
/// Translate a Clang Expr* (from a contract specifier) into a DTT ConstraintExprPtr.
/// Returns nullptr if the expression contains unrecognizable constructs.
ConstraintExprPtr translateContractExpr(const Expr *E, const FunctionDecl *F);
```

This recursively walks the `Expr*` AST:

| Clang AST Node | Action |
|-----------------|--------|
| `CXXMemberCallExpr` | Look up callee in `MethodAnnotations` → substitute `.returns` or `.boolPostconds` |
| `CallExpr` (free fn) | Look up in `FreeFunctionAnnotations` |
| `BinaryOperator` | Map opcode to `ConstraintExpr::Kind` (CmpLT, Add, etc.) |
| `UnaryOperator '!'` | Wrap in `LogicalNot` |
| `DeclRefExpr` (param) | Map to `ArgRef(param_index)` |
| `IntegerLiteral` | Map to `Literal(value)` |
| `CXXBoolLiteralExpr` | Map to `Literal(0/1)` |
| `CXXNullPtrLiteralExpr` | Map to `Literal(0)` |
| `ImplicitCastExpr` | Skip through, translate child |
| `ParenExpr` | Skip through, translate child |
| Anything else | Return `nullptr` (unknown → skip, don't guess) |

### 7.2 Hook Points

**For contracts** — In `TraverseFunctionDecl`, after the existing `AnnotateAttr` loop:

```cpp
// Existing: process [[clang::annotate("dtt:requires=...")]]
for (auto *A : F->attrs()) {
    if (auto *Ann = dyn_cast<AnnotateAttr>(A)) { ... }
}

// NEW: process C++26 pre/post specifiers
for (auto *A : F->attrs()) {
    if (auto *Pre = dyn_cast<PreAttr>(A)) {
        if (auto expr = translateContractExpr(Pre->getCondition(), F))
            preconditions.push_back(expr);
    }
    if (auto *Post = dyn_cast<PostAttr>(A)) {
        if (auto expr = translateContractExpr(Post->getCondition(), F))
            // ... handle as postcondition, resolving Post->getReturnName()
    }
}
```

**For `contract_assert`** — In the statement visitor (similar to how `if`-conditions are processed):

```cpp
// NEW: contract_assert(expr) as proof hint
if (auto *CA = dyn_cast<ContractAssertStmt>(S)) {
    if (auto expr = translateContractExpr(CA->getCondition(), CurrentFunction))
        injectAsProofFact(expr);
}
```

**For concepts** — In `TraverseFunctionDecl` or `VisitFunctionTemplateDecl`, extract concept constraints from the template parameter list and `requires` clause:

```cpp
// NEW: extract trackable properties from concept constraints
if (auto *FTD = F->getDescribedFunctionTemplate()) {
    for (auto *TP : *FTD->getTemplateParameters()) {
        if (auto *TTP = dyn_cast<TemplateTypeParmDecl>(TP)) {
            if (auto *CE = TTP->getTypeConstraint()) {
                inferTrackableProperties(CE->getNamedConcept(), TTP);
            }
        }
    }
}
```

### 7.3 Concept Property Inference

A mapping from standard concepts to trackable properties:

```cpp
llvm::StringMap<SmallVector<StringRef, 4>> ConceptToProperties = {
    {"sized_range",            {"_size"}},
    {"contiguous_range",       {"_size", "_nonnull"}},
    {"random_access_range",    {"_size"}},     // enables bounds checking
    {"sortable",               {"_sorted"}},
};
```

For user-defined concepts, DTT walks the `RequiresExpr` body and looks for expressions that match known method patterns (`.size()`, `.empty()`, `.has_value()`, etc.). Each recognized method implies a trackable property.

### 7.4 What Is NOT Changed

- `ConstraintExprPtr` type and all node kinds — unchanged
- `MethodAnnotations`, `FreeFunctionAnnotations` — unchanged (now also read by the new translator)
- Z3 solver integration — unchanged
- Diagnostic categories (A-H) — unchanged
- `expandConcepts()` / `ConceptRegistry` — unchanged (parallel system for DTT's own concept vocabulary)
- All existing `[[clang::annotate]]` processing — unchanged

## 8. Implementation Phases

### Phase 1: Contract Expression Translator  ✓ IMPLEMENTED

- Implemented `translateContractExpr()` — Expr* to ConstraintExprPtr translator
- Handles: member calls (with MethodAnnotations lookup), binary ops (all 13 opcodes),
  unary `!`, param refs (ParmVarDecl → ArgRef), literals, nullptr, GNU null
- Defined after `FreeFunctionAnnotations` in DTTPlugin.cpp (forward-declared earlier)
- Ready for `pre`/`post` integration when P2900 lands in Clang

### Phase 2: `contract_assert` as Proof Hint  ✓ IMPLEMENTED (as `dtt_contract_assert`)

- Clang 18 lacks P2900, so implemented via `dtt_contract_assert(bool)` — a no-op
  function whose argument expression DTT analyzes in `VisitCallExpr`
- `injectContractAssertProofs()` reuses `extractAllGuards`, `extractBoolCall`,
  `extractHoldsAlternative` infrastructure
- Handles compound `&&` via recursive splitting
- Tests: `examples/1800_contract_assert_pass.cpp`, `examples/1801_contract_assert_insufficient_fail.cpp`

### Phase 3: Concept-Informed Property Discovery

- Map standard `std::ranges::*` concepts to trackable properties
- Walk user-defined concept `RequiresExpr` bodies for method patterns
- Use discovered properties to enable tracking on generic type parameters
- Tests: `examples/2020_concept_property_infer_pass.cpp`, `*_fail.cpp`

### Phase 4: Deep Concept Body Analysis

- Nested concept requirements (concept A requires concept B)
- Compound `requires` clauses (`requires (C1<T> && C2<T>)`)
- Abbreviated function templates (`std::ranges::sized_range auto& r`)
- Tests: `examples/2030_concept_deep_infer_pass.cpp`, `*_fail.cpp`

## 9. Compiler Requirements

| Feature | Minimum Compiler |
|---------|-----------------|
| Contracts (`pre`/`post`/`contract_assert`) | Clang with P2900 support (experimental fork exists; mainline TBD) |
| Concepts (`concept`, `requires`) | Clang 18 (already DTT's baseline — ready now) |
| Existing `[[clang::annotate]]` | Clang 14+ (no change) |

**Phase 3 (concepts) can ship today** — it only requires C++20 support, which DTT's baseline Clang 18 already has. Phases 1-2 (contracts) require waiting for Clang's P2900 implementation to stabilize, but the design is ready.

## 10. Risks

| Risk | Mitigation |
|------|-----------|
| Clang's contract AST representation is experimental and may change | Keep `translateContractExpr()` behind a `#if` for Clang version; update as AST stabilizes |
| Method resolution in contracts may differ from runtime tracking (e.g., overloads) | Use the same `MethodAnnotations` lookup; if ambiguous, skip (conservative) |
| Concept body analysis may be fragile across template instantiation | Start with named standard concepts (Phase 3); defer deep body analysis to Phase 4 |
| `contract_assert` in loops could create unsound proof facts | Scope proof facts the same way `if`-guards are scoped — within the block, not after it |
| `post` evaluated after locals destroyed — parameter values may differ from entry | Match C++26 semantics: only infer from `const` parameters in postconditions |

## 11. Open Questions

1. **Unrecognized expressions**: When a contract contains an expression DTT can't translate (e.g., a user-defined function call), should DTT silently skip it or warn? Current proposal: skip silently (conservative — unknown means untracked, not unsafe).

2. **`contract_assert` vs `assert`**: Should DTT also treat `assert()` from `<cassert>` the same way? It already uses `if`-guards; `assert` is semantically similar. This is orthogonal to C++26 contracts but could be done in the same framework.

3. **Concept subsumption**: If concept A subsumes concept B, and both are constrained, should DTT infer properties from both? Or only the most refined? (Probably both — more information is better.)

4. **`post` with side effects**: A postcondition like `post(r : r.size() == v.size())` references both the return value and a parameter. DTT needs to resolve both — `r` via `ResultRef`, `v` via the parameter's tracked state at function exit. This requires careful handling of `old()` semantics even without explicit `old()`.

5. **Integration with `ignore` evaluation semantic**: When contracts are compiled with `-fcontract-semantic=ignore`, they exist in the AST but are never evaluated at runtime. DTT still reads them. This means DTT provides static checking even when runtime checking is off — which is arguably the most valuable configuration. Worth documenting prominently.
