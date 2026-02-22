# DTT Safety Roadmap: C++ Core Guidelines & GSL-Inspired Enhancements

A unified vision for expanding DTT's compile-time safety detection, synthesizing insights from:
- C++ Core Guidelines Profiles (Type, Bounds, Lifetime)
- P3081/P3402 (C++26 Safety Profiles)
- GSL (Guidelines Support Library) safety concepts
- Herb Sutter's Lifetime Profile paper

---

## Executive Summary

DTT currently excels at **lifetime safety** (null deref, use-after-free, double-free, iterator invalidation) and **bounds checking** (semantic verification of array access). However, significant gaps exist in:

| Category | Status | Priority |
|----------|--------|----------|
| **Lifetime Safety** | Strong | Maintain |
| **Bounds Safety** | Strong (semantic) | Extend to universal |
| **Type Safety** | Weak (21 gaps) | High priority |
| **Arithmetic Safety** | Not implemented | High priority |
| **Resource Management** | Strong (deferred guard eval) | Maintain |
| **Concurrency** | Not implemented | Research |

**Key Insight**: GSL provides *runtime* enforcement. DTT can provide *compile-time* detection of the same violations—catching bugs before they run.

---

## Part 1: DTT's Core Strengths

### What DTT Does Well

DTT's property tracking model (`_size`, `_nonnull`, `_has_value`, `_variant_index`) enables powerful compile-time verification:

| Capability | Property | Example |
|------------|----------|---------|
| Null safety | `_nonnull` | `*p` requires `_nonnull == 1` |
| Bounds checking | `_size`, `_offset` | `v[i]` requires `i < _size` |
| Optional access | `_has_value` | `opt.value()` requires `_has_value == 1` |
| Variant safety | `_variant_index` | `get<T>(v)` requires correct index |
| Use-after-free | `_freed` | Access requires `_freed == 0` |
| Double-free | `_freed` | `delete` requires `_freed == 0` |

### Fully Implemented Safety Rules

| Rule | DTT Error | Description |
|------|-----------|-------------|
| Lifetime.1a | `ErrNullDeref` | Null pointer dereference |
| Lifetime.1b | `ErrUseAfterFree` | Access after deallocation |
| Lifetime.1c | `ErrDoubleFree` | Double deallocation |
| Lifetime.1d | Scope tracking | Dangling stack pointers |
| Lifetime.2 | Iterator tracking | Container mutation invalidation |
| Bounds | `ErrBufferOOB` | Out-of-bounds array access |

---

## Part 2: Type Safety Profile

### Official Sources
- **C++ Core Guidelines Profiles**: Type, Bounds, Lifetime
- **P3081 (C++26)**: Core safety profiles specification
- **P3402**: Initialization safety profile
- **GSL**: Guidelines Support Library types
- **clang-tidy**: cppcoreguidelines-* checks
- **MSVC C++ Core Check**: C26XXX warning codes

### Type.1: Don't use `reinterpret_cast`

| Aspect | Status |
|--------|--------|
| **DTT Status** | Not implemented |
| **Exceptions** | Cast to `std::byte*`, `char*`, `unsigned char*` for serialization |
| **Complexity** | Low |

**Implementation**: Visit `CXXReinterpretCastExpr`, check destination type against allowlist.

### Type.2: Don't use `static_cast` for downcasts

| Aspect | Status |
|--------|--------|
| **DTT Status** | Not implemented |
| **Complexity** | Medium |

**Implementation**: Visit `CXXStaticCastExpr`, detect if source/dest are in inheritance relationship with dest being derived.

### Type.3: Don't use `const_cast` to cast away const

| Aspect | Status |
|--------|--------|
| **DTT Status** | Not implemented |
| **Complexity** | Low |

**Implementation**: Visit `CXXConstCastExpr`, check if removing const/volatile.

### Type.4: Don't use C-style casts

| Aspect | Status |
|--------|--------|
| **DTT Status** | Not implemented |
| **Complexity** | Low |

**Implementation**: Visit `CStyleCastExpr` and `CXXFunctionalCastExpr`.

### Type.5: Don't use a variable before initialization

| Aspect | Status |
|--------|--------|
| **DTT Status** | Partial — scalars and POD struct members tracked |
| **Gap** | Conditional initialization with all-paths analysis |
| **Complexity** | High (requires dataflow analysis) |

### Type.6: Always initialize member variables

| Aspect | Status |
|--------|--------|
| **DTT Status** | Not implemented |
| **Complexity** | Medium |

**Implementation**: Check constructors for missing member initializations.

### Type.7: Avoid naked `union`

| Aspect | Status |
|--------|--------|
| **DTT Status** | Not implemented (`std::variant` tracked via `_variant_index`) |
| **Complexity** | Medium |

**Implementation**: Visit `MemberExpr` on union types, track which member was last written.

### Type.8: Avoid varargs

| Aspect | Status |
|--------|--------|
| **DTT Status** | Not implemented |
| **Complexity** | Low |

**Implementation**: Flag functions with `...` parameter, and any use of `va_list`/`va_start`/`va_arg`.

---

## Part 3: Bounds Safety Profile

### Bounds.1: Don't use pointer arithmetic

| Aspect | Status |
|--------|--------|
| **DTT Status** | Partial — checks result safety, not style |
| **Current** | Detects OOB via `_size`/`_offset` tracking |

DTT proves safety semantically. Profile wants syntactic prohibition. Optional `--warn-pointer-arithmetic` flag could add style enforcement.

### Bounds.2: Only index arrays with constant expressions

| Aspect | Status |
|--------|--------|
| **DTT Status** | Not enforced (by design) |
| **DTT Approach** | Allows symbolic indices, proves bounds symbolically |

Philosophical difference — DTT's approach is more powerful (allows runtime indices if provably safe).

### Bounds.3: No array-to-pointer decay

| Aspect | Status |
|--------|--------|
| **DTT Status** | Not implemented |
| **Complexity** | Low |

**Implementation**: Visit `ImplicitCastExpr` with `CK_ArrayToPointerDecay`. DTT intentionally tracks size through decay; this would be an optional stricter check.

### Bounds.4: Don't use non-bounds-checked functions/types

| Aspect | Status |
|--------|--------|
| **DTT Status** | Partial — semantic checking, not style |
| **Current** | Verifies bounds at each `[]` access |

---

## Part 4: Lifetime Safety Profile

### Lifetime.1: Don't dereference invalid pointers

| Aspect | Status |
|--------|--------|
| **DTT Status** | Strong implementation |
| **Checks** | `ErrNullDeref`, `ErrUseAfterFree`, `ErrDoubleFree`, `ErrBufferOOB` |

Core lifetime rules well-implemented in DTT.

### Lifetime.2: Iterator invalidation

| Aspect | Status |
|--------|--------|
| **DTT Status** | Implemented |
| **Tracking** | `_size` changes invalidate iterators |

### Lifetime.3: Dangling references from temporaries

| Aspect | Status |
|--------|--------|
| **DTT Status** | Partial |
| **Gap** | Complex temporary lifetime scenarios |
| **Complexity** | High |

---

## Part 5: Arithmetic and Initialization Profiles (P3081/P3402)

### Arithmetic.1: Signed integer overflow

Not implemented. High complexity (requires value range analysis + Z3).

### Arithmetic.2: Narrowing conversions

Not implemented. Medium complexity. GSL equivalent: `gsl::narrow<T>()`.

### Arithmetic.3: Signed/unsigned mixing

Not implemented. Medium complexity.

### Init.1: No vacuous initialization (P3402)

Not implemented. C++26 defines erroneous behavior for uninitialized reads.

---

## Part 6: GSL Type Support

| GSL Type | DTT Status | Implementation |
|----------|------------|----------------|
| `gsl::not_null<T>` | Not tracked | Infer `_nonnull = 1` automatically |
| `gsl::span<T>` | Tracked as `std::span` | Equivalent |
| `gsl::owner<T*>` | Not tracked | Track ownership through `gsl::owner` |
| `gsl::narrow<T>` | Not tracked | Checked narrowing cast |

---

## Part 7: Concurrency and Resource Safety

### Data Race Detection
Not implemented (very high complexity). Most tools use dynamic analysis (TSan).

### Deadlock Detection
Not implemented (very high complexity). Requires lock order analysis.

### Resource Management (R.1/R.3/R.5)
Partial — tracks FILE*, malloc/free, smart pointers. Generic memory leak detection (new without delete) and unnecessary heap allocation warnings not yet implemented.

---

## Part 8: Gap Summary

### Not Implemented (21 items)
- **Type safety (7)**: Type.1-4 casts, Type.6 member init, Type.7 union, Type.8 varargs
- **Bounds safety (2)**: Bounds.3 array decay, Bounds.4 unchecked subscript style
- **Arithmetic safety (3)**: Signed overflow, narrowing, signed/unsigned mixing
- **Initialization (2)**: Init.1 vacuous init, Type.5 full all-paths analysis
- **GSL types (3)**: gsl::not_null, gsl::owner, gsl::narrow
- **Concurrency (2)**: Data races, deadlocks
- **Resource (2)**: Memory leak detection, R.5 unnecessary heap allocation

### Partial Implementation (5 items)
1. **Type.5** — Uninitialized variables (scalars + POD members done; all-paths remaining)
2. **Bounds.1** — Pointer arithmetic (semantic, not style)
3. **Bounds.2** — Non-constant indices (allows symbolic proofs — by design)
4. **Lifetime.3** — Dangling temporaries (some cases)
5. **R.1/R.3** — RAII/resource management (FILE*, malloc, smart pointers done)

### Fully Implemented (6 items)
1. **Lifetime.1a-d** — Null deref, use-after-free, double-free, dangling pointers
2. **Lifetime.2** — Iterator invalidation
3. **Bounds** — Semantic bounds checking

### Proposed New Properties

| Property | Type | Tracks | Used For |
|----------|------|--------|----------|
| `_offset` | int | Pointer displacement | Pointer arithmetic |
| `_owns_resource` | bool | Memory ownership | Leak detection |
| `_points_to` | string | Pointee identity | Iterator invalidation |
| `_valid` | bool | Pointee still exists | Dangling detection |
| `_min_value` | int | Numeric lower bound | Narrowing detection |
| `_max_value` | int | Numeric upper bound | Narrowing detection |
| `_active_member` | string | Union active field | Union safety |
| `_state` | enum | Object protocol state | Type state |
| `_tainted` | bool | Untrusted data | Input validation |

---

## Part 9: Implementation Roadmap

### Phase 1: Quick Wins (1-2 weeks)

| Item | Effort | Impact | Notes |
|------|--------|--------|-------|
| Type.1 `reinterpret_cast` | 1 day | High | Simple AST visitor |
| Type.3 `const_cast` | 1 day | High | Simple AST visitor |
| Type.4 C-style cast | 1 day | High | Simple AST visitor |
| Type.8 varargs | 1 day | Medium | Flag va_list usage |
| Bounds.3 array decay | 1 day | Medium | ImplicitCastExpr check |
| Reference-implies-nonnull | 1 day | Medium | `&x` always non-null |
| C-array size tracking | 2 days | High | `int arr[10]` → `_size=10` |

### Phase 2: Core Enhancements (1-2 months)

| Item | Effort | Impact | Notes |
|------|--------|--------|-------|
| Narrowing detection | 1 week | High | `_min_value`, `_max_value` |
| Union member tracking | 1 week | Medium | `_active_member` |
| Pointer arithmetic bounds | 2 weeks | High | `_offset`, `_base` |
| Iterator invalidation (enhanced) | 2 weeks | High | `_points_to` tracking |
| Ownership inference | 2 weeks | High | `_owns_resource` from `new` |

### Phase 3: Advanced Analysis (2-3 months)

| Item | Effort | Impact | Notes |
|------|--------|--------|-------|
| Full uninitialized detection | 1 week | High | Basic done; conditional init remaining |
| Signed overflow detection | 3 weeks | High | Z3 overflow proving |
| Memory leak detection | 3 weeks | High | Scope-based escape analysis |
| Dangling temporaries | 3 weeks | Medium | Complex lifetime |

### Phase 4: Research (6+ months)

| Item | Effort | Impact | Notes |
|------|--------|--------|-------|
| Taint tracking | 2 months | High | Full data flow |
| Type state machines | 2 months | Medium | Generalized protocols |
| Data race detection | 6+ months | Very High | Requires concurrency model |
| Deadlock detection | 6+ months | Medium | Lock order analysis |

---

## Part 10: DTT vs GSL Philosophy

| Aspect | GSL | DTT |
|--------|-----|-----|
| **When** | Runtime | Compile-time |
| **Cost** | Runtime overhead | Zero runtime cost |
| **Coverage** | Only GSL-annotated code | All code |
| **Failure mode** | Exception/terminate | Compilation error |
| **False positives** | None (runtime truth) | Possible (static approximation) |
| **False negatives** | Possible (dynamic paths) | Possible (analysis limits) |

**DTT's Advantage**: Bugs caught at compile time never ship. No runtime overhead in production.

**GSL's Advantage**: No false positives; exact runtime behavior.

**Best Practice**: Use both—DTT for compile-time guarantees, GSL for runtime defense-in-depth.

---

## Part 11: Circle Compiler Insights

The Circle compiler (Sean Baxter) implements Rust-style borrow checking for C++. While its Safe C++ proposal was abandoned by the ISO committee in September 2025, Circle's techniques offer valuable lessons for DTT.

### 11.1 Circle's Safety Model

Circle implements **ownership and borrowing** with compile-time enforcement:

| Concept | Circle | DTT Equivalent |
|---------|--------|----------------|
| Ownership | `T^` (owning pointer) | `_owns_resource` property |
| Mutable borrow | `T^` (mutable ref) | Not tracked |
| Shared borrow | `const T^` (shared ref) | Not tracked |
| Lifetime parameter | `'a` annotations | Not tracked |
| Exclusivity | One mutable OR many shared | Not tracked |

**Circle's Key Invariant**: "There may be one mutable reference to a place, or any number of shared references, but not both at the same time."

This prevents data races and iterator invalidation at the type system level.

### 11.2 Borrow Checker vs DTT Lifetime Tracking

| Aspect | Circle Borrow Checker | DTT Lifetime Tracking |
|--------|----------------------|----------------------|
| **Mechanism** | Region variables with constraint solving | Property tracking (`_nonnull`, `_freed`) |
| **Scope** | All references, all lifetimes | Pointers, optional types, containers |
| **Aliasing** | Tracks all aliases, enforces exclusivity | No alias tracking |
| **Invalidation** | Implicit from borrow conflicts | Explicit per-container rules |
| **Analysis level** | MIR (mid-level IR) dataflow | AST-level traversal |
| **False positives** | Possible (Rust-style "fighting the borrow checker") | Rare (conservative approach) |

**Circle's Advantage**: Comprehensive lifetime tracking catches more bugs.

**DTT's Advantage**: Works with existing code, no syntax changes, no "borrow checker fights."

### 11.3 Actionable DTT Enhancements (from Circle's Lessons)

#### A. Exclusivity Tracking — Prevents Iterator Invalidation (High Priority)

**The Problem**: References/iterators to container elements become invalid when the container reallocates.

**DTT Implementation**:

```cpp
std::vector<int> v = {1, 2, 3};
int& r = v[0];       // DTT: v._has_refs = 1, r._refs_into = "v"
v.push_back(4);      // DTT ERROR: cannot mutate v while references exist

auto it = v.begin(); // DTT: v._has_refs = 1, it._refs_into = "v"
v.clear();           // DTT ERROR: cannot invalidate v while iterators exist
*it;                 // Would crash at runtime
```

**New Properties**:
| Property | Type | Meaning |
|----------|------|---------|
| `_has_refs` | bool | Container has outstanding references/iterators |
| `_refs_into` | string | What container this reference points into |

**AST Implementation**:
1. On `v[i]`, `v.data()`, `v.begin()`: set `v._has_refs = 1`
2. On potentially-invalidating calls (`push_back`, `insert`, `clear`, `resize`): require `_has_refs == 0`
3. Track reference scope; on scope exit, decrement ref count

**Invalidation Table** (encode in `annotations/std.dtt`):

| Container | Invalidating Operations |
|-----------|------------------------|
| `vector` | `push_back`, `insert`, `emplace`, `reserve`, `resize`, `clear` |
| `deque` | `push_front`, `push_back`, `insert`, `erase` |
| `string` | `+=`, `append`, `insert`, `resize`, `clear` |
| `map/set` | `erase` (only erased element) |
| `list` | `erase` (only erased element) |

#### B. Initialization Analysis — Track All Types (High Priority)

**The Problem**: Uninitialized non-pointer variables not tracked (only pointers via `_nonnull`).

**DTT Implementation**:

```cpp
int x;               // DTT: x._initialized = 0
if (cond) x = 1;     // DTT: x._initialized = UNKNOWN (path-dependent)
return x;            // DTT ERROR: x may be uninitialized

int y;
if (cond) y = 1;
else y = 2;
return y;            // OK: all paths initialize y
```

**Implementation Strategy**:
1. On `VarDecl` without initializer: set `var._initialized = 0`
2. On assignment: set `var._initialized = 1`
3. On use: require `var._initialized == 1`
4. For conditionals: use existing guard system + Z3 to prove all paths

**Key Insight**: DTT already tracks guards through if/else—extend to track initialization state per branch, merge at join points.

#### C. Return Reference Tracking — Catch Dangling Returns (Medium Priority)

**The Problem**: Functions returning references to parameters; callers may pass temporaries.

**DTT Implementation**:

```cpp
// Annotation declares return depends on parameter lifetime
[[clang::annotate("dtt:returns_ref_from=v")]]
int& front(std::vector<int>& v) { return v[0]; }

// At call site:
std::vector<int> make_vec();
int& r = front(make_vec());  // DTT ERROR: temporary dies, r dangles
use(r);                      // Would crash at runtime
```

**New Annotation**: `dtt:returns_ref_from=PARAM_NAME`

**Implementation**:
1. Parse annotation, record return-depends-on relationship
2. At call site: if argument is temporary (rvalue), error
3. If argument is local, track that return value's validity ends when local dies

**Pre-Annotate STL** (in `annotations/std.dtt`):
- `vector::front()`, `vector::back()`, `vector::operator[]` → returns ref from `this`
- `string::front()`, `string::back()`, `string::operator[]` → returns ref from `this`
- `optional::value()` → returns ref from `this`

#### D. Exhaustive Variant Visit — Low Effort, Medium Value

**The Problem**: `std::visit` with incomplete handler set compiles but may fail at runtime.

**DTT Implementation**:

```cpp
std::variant<int, string, double> v;
std::visit(overloaded{
    [](int i) { },
    [](string s) { },
    // Missing double handler!
}, v);  // DTT ERROR: visit is non-exhaustive, missing handler for double
```

**Implementation**:
1. On `std::visit(visitor, variant)` call:
2. Extract variant's alternative types from template arguments
3. Check visitor's `operator()` overloads cover all types
4. Emit error listing missing handlers

**Complexity**: Medium—requires template argument inspection and overload resolution analysis.

### 11.4 Gap Coverage: DTT Implementation Strategies

| Gap (from Parts 2-9) | DTT Implementation | Effort | Notes |
|----------------------|-------------------|--------|-------|
| **Type.1-4 (Casts)** | Visit cast AST nodes, emit warnings | 1 day each | Already planned, straightforward |
| **Type.5-6 (Uninit)** | `_initialized` property + guard merging | 2 weeks | Reuse existing guard infrastructure |
| **Type.7 (Union)** | `_active_member` tracking | 1 week | Already planned |
| **Type.8 (Varargs)** | Flag `va_list` usage | 1 day | Already planned |
| **Bounds** | `_size` tracking | ✓ Done | DTT strength |
| **Arithmetic** | `_min_value`/`_max_value` + Z3 | 2 weeks | Already planned |
| **Lifetime (basic)** | `_nonnull`, `_freed` | ✓ Done | DTT strength |
| **Lifetime (refs)** | `_has_refs` + invalidation rules | 2 weeks | **New from Circle insight** |
| **Lifetime (returns)** | `returns_ref_from` annotation | 1 week | **New from Circle insight** |
| **Ownership** | `_owns_resource` inference | 2 weeks | Already planned |

### 11.5 Approaches DTT Should Avoid

| Approach | Why Unsuitable for DTT |
|----------|------------------------|
| **Explicit lifetime parameters** | Requires annotation on every function; too invasive |
| **New reference/pointer types** | Not standard C++; existing code won't compile |
| **Separate IR pass (MIR)** | Over-engineering; AST-level analysis is sufficient |
| **Parallel standard library** | Defeats zero-friction adoption |
| **Mandatory safe/unsafe blocks** | Requires language extension |

**DTT's Core Constraint**: Must work with `clang++ -fplugin=...` on unmodified source.

### 11.6 Updated Priority Matrix (with Circle Insights)

| Feature | Original | Revised | Effort | Rationale |
|---------|----------|---------|--------|-----------|
| **Exclusivity/ref tracking** | — | **P0** | 2 weeks | Biggest gap; prevents iterator invalidation |
| **Initialization analysis** | P2 | **P0** | 2 weeks | High-value, reuses guard system |
| **Return ref annotation** | — | **P1** | 1 week | Catches dangling return values |
| **Exhaustive visit** | — | **P2** | 3 days | Low effort, catches real bugs |
| Cast detection (Type.1-4) | P0 | P0 | 4 days | Unchanged; still quick win |
| Narrowing detection | P0 | P0 | 2 weeks | Unchanged |
| Pointer arithmetic bounds | P1 | P1 | 2 weeks | Unchanged |
| Union tracking | P1 | P1 | 1 week | Unchanged |

**New P0 items from Circle analysis**: Exclusivity tracking, Initialization analysis

### 11.7 Design Principles for DTT (Lessons from Circle)

Circle's Safe C++ proposal was rejected by ISO in 2025. DTT must learn from this:

| Circle's Mistake | DTT's Approach |
|------------------|----------------|
| Required new syntax (`safe`, `unsafe`, `T^`) | Zero syntax changes; annotations are optional |
| Required new standard library (`std2`) | Works with existing `std::` types |
| All-or-nothing safety model | Gradual: `inform` → `enforce` modes |
| Steep learning curve (lifetime annotations) | Familiar: preconditions/postconditions |
| Separate compiler | Standard Clang plugin |

**DTT's Competitive Advantages to Preserve**:
1. **Drop-in**: `clang++ -fplugin=libDTTPlugin.so` — no other changes
2. **Additive**: Warns about new things; never breaks working code
3. **Gradual**: Start with warnings, tighten incrementally
4. **Familiar**: Uses standard C++ annotation syntax
5. **Interoperable**: Analyzed and non-analyzed code coexist

### 11.8 Implementation Checklist

Concrete steps to implement Circle-inspired enhancements:

#### Exclusivity Tracking (P0)
- [ ] Add `_has_refs` property to container types in `annotations/std.dtt`
- [ ] Add `_refs_into` property for references/iterators/pointers
- [ ] Track reference creation in `VisitUnaryOperator` (address-of) and subscript
- [ ] Add invalidation annotations to mutating methods (`push_back`, etc.)
- [ ] Implement scope-based reference counting (decrement on scope exit)
- [ ] Write tests: `*_iterator_invalidation_fail.cpp`, `*_ref_invalidation_fail.cpp`

#### Initialization Analysis (P0) — Basic tracking DONE
- [x] Add `_initialized` property type
- [x] Set `_initialized = 0` on `VarDecl` without initializer
- [x] Set `_initialized = 1` on assignment
- [x] POD struct member tracking (individual, nested, partial init)
- [ ] Extend guard merge logic for conditional initialization
- [ ] Use Z3 for "all paths initialize" proofs
- [ ] Write tests: `*_conditional_init_pass.cpp`

#### Return Reference Tracking (P1)
- [ ] Define `dtt:returns_ref_from=PARAM` annotation syntax
- [ ] Parse annotation in `handleAnnotation()`
- [ ] At call site: check if argument is temporary (dies immediately)
- [ ] At call site: track return value validity bound to argument lifetime
- [ ] Pre-annotate STL accessors in `annotations/std.dtt`
- [ ] Write tests: `*_dangling_return_fail.cpp`

#### Exhaustive Visit (P2)
- [ ] Detect `std::visit` calls in `VisitCallExpr`
- [ ] Extract variant type alternatives from template arguments
- [ ] Analyze visitor's `operator()` overload set
- [ ] Report missing type handlers
- [ ] Write tests: `*_nonexhaustive_visit_fail.cpp`

### 11.9 Conclusion

**Key Insight**: DTT can achieve ~85% of borrow checking's safety benefits through property tracking, without lifetime annotations or syntax changes.

| Approach | Lifetime Coverage | Adoption Friction |
|----------|-------------------|-------------------|
| Full borrow checking (Circle) | ~95% | Very High (new syntax, new stdlib) |
| **Property tracking (DTT + enhancements)** | **~85%** | **Zero** (drop-in plugin) |
| No static analysis | ~0% | None |

The 10% gap (complex cross-function lifetime flows) is acceptable given DTT's zero-friction adoption model. Users needing 100% coverage should also use runtime tools (ASan, valgrind) as defense-in-depth.

---

## References

### Standards & Proposals
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [P3081 R2: Core safety profiles for C++26](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3081r2.pdf)
- [P3402: Initialization Safety Profile](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3402r2.html)

### GSL & Lifetime Analysis
- [GSL: Guidelines Support Library](https://github.com/microsoft/GSL)
- [Lifetime Profile v1.0 (Herb Sutter)](https://herbsutter.com/2018/09/20/lifetime-profile-v1-0-posted/)
- [Implementing Lifetime Safety in Clang](https://llvm.org/devmtg/2019-04/slides/TechTalk-Horvath-Implementing_the_C++_Core_Guidelines_Lifetime.pdf)

### Tools & Implementations
- [clang-tidy cppcoreguidelines checks](https://clang.llvm.org/extra/clang-tidy/checks/list.html)
- [MSVC C++ Core Check](https://learn.microsoft.com/en-us/cpp/code-quality/code-analysis-for-cpp-corecheck)

### Critical Analysis
- [Why Safety Profiles Failed (Sean Baxter)](https://www.circle-lang.org/draft-profiles.html)
- [Safe C++ Proposal](https://safecpp.org/P3390R0.html)
- [C++ Safety in Context (Herb Sutter)](https://herbsutter.com/2024/03/11/safety-in-context/)

### Circle Compiler & Safe C++
- [Circle Compiler](https://github.com/seanbaxter/circle) — Sean Baxter's C++ compiler with memory safety
- [Safe C++ Draft](https://safecpp.org/draft.html) — Borrow checking proposal for C++
- [Memory Safety without Lifetime Parameters](https://safecpp.org/draft-lifetimes.html) — Alternative lifetime inference
- [Safe C++ Proposal Abandoned (The Register, Sept 2025)](https://www.theregister.com/2025/09/16/safe_c_proposal_ditched/) — ISO committee decision
- [Sean Baxter at C++ Alliance](https://cppalliance.org/people/sean.html) — Author background
