# C++ Safety Profiles: Comprehensive Gap Analysis

This document provides a complete comparison between official C++ safety profiles and DTT's current implementation.

## Official Profile Sources

- **C++ Core Guidelines Profiles**: Type, Bounds, Lifetime
- **P3081 (C++26)**: Core safety profiles specification
- **P3402**: Initialization safety profile
- **GSL**: Guidelines Support Library types
- **clang-tidy**: cppcoreguidelines-* checks
- **MSVC C++ Core Check**: C26XXX warning codes

---

# Part 1: Type Safety Profile

## Type.1: Don't use `reinterpret_cast`

| Aspect | Status |
|--------|--------|
| **Rule** | Flag all uses of `reinterpret_cast` |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Exceptions** | Cast to `std::byte*`, `char*`, `unsigned char*` for serialization; cast to/from `uintptr_t` |
| **Complexity** | Low |

```cpp
// Should flag:
void* p = ...;
int* ip = reinterpret_cast<int*>(p);  // Type.1 violation

// Allowed exceptions:
std::byte* bp = reinterpret_cast<std::byte*>(&x);  // OK for serialization
```

**Implementation**: Visit `CXXReinterpretCastExpr`, check destination type against allowlist.

---

## Type.2: Don't use `static_cast` for downcasts

| Aspect | Status |
|--------|--------|
| **Rule** | Use `dynamic_cast` instead of `static_cast` for downcasting |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Rationale** | Static downcast assumes correct type without verification |
| **Complexity** | Medium |

```cpp
struct Base { virtual ~Base() = default; };
struct Derived : Base { int x; };

Base* b = ...;
Derived* d = static_cast<Derived*>(b);   // Type.2 violation - no runtime check
Derived* d = dynamic_cast<Derived*>(b);  // Safe - returns nullptr if wrong type
```

**Implementation**: Visit `CXXStaticCastExpr`, detect if source/dest are in inheritance relationship with dest being derived.

---

## Type.3: Don't use `const_cast` to cast away const

| Aspect | Status |
|--------|--------|
| **Rule** | Flag `const_cast` that removes const qualifier |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Rationale** | Modifying const data is undefined behavior |
| **Complexity** | Low |

```cpp
const int* cp = &x;
int* p = const_cast<int*>(cp);  // Type.3 violation
*p = 42;  // UB if original object was const
```

**Implementation**: Visit `CXXConstCastExpr`, check if removing const/volatile.

---

## Type.4: Don't use C-style casts

| Aspect | Status |
|--------|--------|
| **Rule** | Flag `(T)expr` and functional `T(expr)` casts |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Rationale** | C-style casts hide dangerous conversions |
| **Complexity** | Low |

```cpp
void* p = ...;
int* ip = (int*)p;           // Type.4 violation - C-style cast
int* ip = int*(p);           // Type.4 violation - functional cast
int i = static_cast<int>(f); // OK - explicit about conversion type
```

**Implementation**: Visit `CStyleCastExpr` and `CXXFunctionalCastExpr`.

---

## Type.5: Don't use a variable before initialization

| Aspect | Status |
|--------|--------|
| **Rule** | All variables must be initialized before use |
| **DTT Status** | ⚠️ **PARTIAL** - Only pointers tracked |
| **Current** | `ErrUninitializedPtr` for pointer types |
| **Gap** | Non-pointer types (int, float, structs) not tracked |
| **Complexity** | High (requires dataflow analysis) |

```cpp
int x;
if (cond) x = 1;
return x;  // Type.5 violation - x may be uninitialized

int* p;
*p = 5;    // DTT catches this (ErrUninitializedPtr)
```

**Gap**: DTT only tracks `_nonnull` for pointers. General uninitialized variable detection requires full definite-assignment analysis.

---

## Type.6: Always initialize member variables

| Aspect | Status |
|--------|--------|
| **Rule** | All class members must be initialized in constructor |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Complexity** | Medium |

```cpp
struct S {
    int x;      // Type.6 - should be initialized
    int* p;     // Type.6 - should be initialized
    S() {}      // Missing initialization of x and p
};
```

**Implementation**: Check constructors for missing member initializations.

---

## Type.7: Avoid naked `union`

| Aspect | Status |
|--------|--------|
| **Rule** | Don't access union members directly; use `std::variant` |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Current** | DTT tracks `std::variant` via `_variant_index` |
| **Gap** | Raw `union` access not flagged |
| **Complexity** | Medium |

```cpp
union U { int i; float f; };
U u;
u.i = 42;
float x = u.f;  // Type.7 violation - type punning via union

std::variant<int, float> v = 42;
float x = std::get<float>(v);  // DTT catches this (wrong index)
```

**Implementation**: Visit `MemberExpr` on union types, track which member was last written.

---

## Type.8: Avoid varargs

| Aspect | Status |
|--------|--------|
| **Rule** | Don't use `va_list`, `va_start`, `va_arg`, `...` parameters |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Rationale** | Varargs bypass type checking entirely |
| **Complexity** | Low |

```cpp
void log(const char* fmt, ...) {  // Type.8 violation
    va_list args;
    va_start(args, fmt);
    int x = va_arg(args, int);    // No type safety
    va_end(args);
}
```

**Implementation**: Flag functions with `...` parameter, and any use of `va_list`/`va_start`/`va_arg`.

---

# Part 2: Bounds Safety Profile

## Bounds.1: Don't use pointer arithmetic

| Aspect | Status |
|--------|--------|
| **Rule** | Flag `p++`, `p--`, `p + n`, `p - n`, `p[n]` on raw pointers |
| **DTT Status** | ⚠️ **PARTIAL** - Checks result safety, not style |
| **Current** | Detects OOB via `_size`/`_offset` tracking |
| **Gap** | Doesn't flag pointer arithmetic as a style violation |
| **Complexity** | Low (to add style check) |

```cpp
int* p = arr;
p++;           // Bounds.1 style violation (DTT: checks if result valid)
int x = p[5];  // Bounds.1 style violation (DTT: checks 5 < _size)

std::span<int> s(arr, 10);
s[5];          // OK - bounds-checked
```

**Current behavior**: DTT proves safety semantically. Profile wants syntactic prohibition.

**Option**: Add `--warn-pointer-arithmetic` flag for style enforcement.

---

## Bounds.2: Only index arrays with constant expressions

| Aspect | Status |
|--------|--------|
| **Rule** | Array index must be compile-time constant |
| **DTT Status** | ❌ **NOT ENFORCED** (by design) |
| **DTT Approach** | Allows symbolic indices, proves bounds symbolically |
| **Complexity** | N/A (philosophical difference) |

```cpp
int arr[10];
int i = get_index();
arr[i];  // Bounds.2 violation (DTT: checks i < 10 symbolically)

arr[5];  // OK - constant index
```

**Note**: This is a philosophical difference. DTT's approach is more powerful (allows runtime indices if provably safe) but less conservative. Could add optional strict mode.

---

## Bounds.3: No array-to-pointer decay

| Aspect | Status |
|--------|--------|
| **Rule** | Flag implicit conversion from `T[N]` to `T*` |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Current** | DTT tracks size through decay (feature, not bug) |
| **Complexity** | Low |

```cpp
void process(int* p, size_t n);

int arr[10];
process(arr, 10);  // Bounds.3 violation - array decayed to pointer

void process(std::span<int> s);
process(arr);      // OK - span preserves size
```

**Implementation**: Visit `ImplicitCastExpr` with `CK_ArrayToPointerDecay`, emit warning.

**Note**: DTT intentionally tracks size through decay. This would be an optional stricter check.

---

## Bounds.4: Don't use non-bounds-checked functions/types

| Aspect | Status |
|--------|--------|
| **Rule** | Flag unchecked `operator[]`, prefer `gsl::at()` |
| **DTT Status** | ⚠️ **PARTIAL** - Semantic checking, not style |
| **Current** | Verifies bounds at each `[]` access |
| **Gap** | Doesn't flag as style violation |
| **Complexity** | Low |

```cpp
std::vector<int> v(10);
v[i];           // Bounds.4 style violation (DTT: checks i < v._size)
v.at(i);        // OK - throws on OOB
gsl::at(v, i);  // OK - throws on OOB
```

**Note**: DTT provides semantic verification. Style check would flag all `[]` without proof.

---

# Part 3: Lifetime Safety Profile

## Lifetime.1: Don't dereference invalid pointers

| Aspect | Status |
|--------|--------|
| **Rule** | No null deref, no dangling deref, no OOB deref |
| **DTT Status** | ✅ **STRONG IMPLEMENTATION** |
| **Checks** | `ErrNullDeref`, `ErrUseAfterFree`, `ErrDoubleFree`, `ErrBufferOOB` |

### Lifetime.1a: Null pointer dereference

```cpp
int* p = nullptr;
*p = 5;  // DTT: ErrNullDeref ✅
```

### Lifetime.1b: Use-after-free

```cpp
int* p = new int(42);
delete p;
*p = 5;  // DTT: ErrUseAfterFree ✅
```

### Lifetime.1c: Double-free

```cpp
int* p = new int(42);
delete p;
delete p;  // DTT: ErrDoubleFree ✅
```

### Lifetime.1d: Dangling pointer (stack)

```cpp
int* p;
{
    int x = 42;
    p = &x;
}
*p = 5;  // DTT: Should catch via lifetime tracking
```

**Status**: Core lifetime rules well-implemented in DTT.

---

## Lifetime.2: Iterator invalidation

| Aspect | Status |
|--------|--------|
| **Rule** | Don't use iterators/pointers after container mutation |
| **DTT Status** | ✅ **IMPLEMENTED** |
| **Tracking** | `_size` changes invalidate iterators |

```cpp
std::vector<int> v = {1, 2, 3};
auto it = v.begin();
v.push_back(4);  // May reallocate
*it = 5;         // DTT: Iterator may be invalidated ✅
```

---

## Lifetime.3: Dangling references from temporaries

| Aspect | Status |
|--------|--------|
| **Rule** | Don't bind reference to temporary that outlives it |
| **DTT Status** | ⚠️ **PARTIAL** |
| **Gap** | Complex temporary lifetime scenarios |
| **Complexity** | High |

```cpp
std::string_view sv = std::string("temp");  // Dangling after statement
const std::string& r = get_string();        // OK if returns ref, UB if returns value
```

**Note**: C++26 adds `[[nodiscard]]` for some cases. Full tracking requires sophisticated dataflow.

---

# Part 4: P3081 Additions (C++26)

## Arithmetic Profile

### Arithmetic.1: Signed integer overflow

| Aspect | Status |
|--------|--------|
| **Rule** | Detect signed overflow (undefined behavior) |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Complexity** | High (requires value range analysis) |

```cpp
int a = INT_MAX;
int b = a + 1;  // UB - signed overflow
```

---

### Arithmetic.2: Narrowing conversions

| Aspect | Status |
|--------|--------|
| **Rule** | Flag implicit narrowing (data loss) |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **GSL** | `gsl::narrow<T>()` throws on narrowing |
| **Complexity** | Medium |

```cpp
int64_t big = 1LL << 40;
int small = big;  // Narrowing - data loss

int safe = gsl::narrow<int>(big);  // Throws if narrowing
```

---

### Arithmetic.3: Signed/unsigned mixing

| Aspect | Status |
|--------|--------|
| **Rule** | Flag mixed signed/unsigned arithmetic |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Rationale** | Source of subtle bugs |
| **Complexity** | Medium |

```cpp
int i = -1;
unsigned u = 1;
if (i < u) { }  // Surprise: false! (-1 converts to large unsigned)
```

---

## Initialization Profile (P3402)

### Init.1: No vacuous initialization

| Aspect | Status |
|--------|--------|
| **Rule** | Default initialization must actually initialize |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **C++26** | Erroneous behavior for uninitialized reads |
| **Complexity** | Medium |

```cpp
int x;        // Vacuous - no initialization
int y = int(); // OK - zero-initialized
int z{};      // OK - value-initialized
```

---

# Part 5: GSL Type Support

## gsl::not_null<T>

| Aspect | Status |
|--------|--------|
| **Purpose** | Pointer that cannot be null |
| **DTT Status** | ❌ **NOT TRACKED** |
| **Gap** | Could infer `_nonnull == 1` automatically |

```cpp
void process(gsl::not_null<int*> p) {
    *p = 42;  // Always safe - p cannot be null
}
```

**Implementation**: When parameter is `gsl::not_null<T>`, set `_nonnull = 1`.

---

## gsl::span<T>

| Aspect | Status |
|--------|--------|
| **Purpose** | Bounds-checked view over contiguous memory |
| **DTT Status** | ✅ **TRACKED** as `std::span` |
| **Note** | `gsl::span` and `std::span` are equivalent |

---

## gsl::owner<T*>

| Aspect | Status |
|--------|--------|
| **Purpose** | Semantic annotation for ownership |
| **DTT Status** | ❌ **NOT TRACKED** |
| **Usage** | Static analysis marker, no runtime effect |

```cpp
gsl::owner<int*> p = new int(42);  // p owns the memory
delete p;  // Must delete through owner

int* q = p;  // q is non-owning (raw pointer)
delete q;    // Should warn - deleting through non-owner
```

**Implementation**: Track ownership through `gsl::owner`, warn on delete through non-owner.

---

## gsl::narrow<T>

| Aspect | Status |
|--------|--------|
| **Purpose** | Checked narrowing cast |
| **DTT Status** | ❌ **NOT TRACKED** |

```cpp
int64_t big = get_value();
int small = gsl::narrow<int>(big);  // Throws if value doesn't fit
```

---

# Part 6: Concurrency Safety

## Data Race Detection

| Aspect | Status |
|--------|--------|
| **Rule** | Flag unsynchronized access to shared data |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Complexity** | Very High |
| **Note** | Even Rust only guarantees this for `Send`/`Sync` types |

```cpp
int shared = 0;
std::thread t1([&]{ shared++; });
std::thread t2([&]{ shared++; });
// Data race - undefined behavior
```

**Note**: Static race detection is extremely difficult. Most tools use dynamic analysis (ThreadSanitizer).

---

## Deadlock Detection

| Aspect | Status |
|--------|--------|
| **Rule** | Flag potential deadlocks |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Complexity** | Very High |

```cpp
std::mutex m1, m2;
// Thread 1: lock(m1); lock(m2);
// Thread 2: lock(m2); lock(m1);
// Potential deadlock
```

---

# Part 7: Resource Management

## R.1: RAII enforcement

| Aspect | Status |
|--------|--------|
| **Rule** | Resources should be managed by RAII objects |
| **DTT Status** | ⚠️ **PARTIAL** |
| **Current** | Tracks FILE*, smart pointers |
| **Gap** | Generic resource leak detection |

---

## R.3: Raw `new`/`delete` pairing

| Aspect | Status |
|--------|--------|
| **Rule** | Flag naked `new`/`delete`, prefer smart pointers |
| **DTT Status** | ⚠️ **PARTIAL** |
| **Current** | `ErrDoubleFree`, `ErrMismatchedDealloc` |
| **Gap** | Memory leak detection (allocated but never freed) |

```cpp
int* p = new int(42);
// ... no delete ...
// Memory leak - not detected
```

---

## R.5: Prefer scoped objects

| Aspect | Status |
|--------|--------|
| **Rule** | Don't heap-allocate unnecessarily |
| **DTT Status** | ❌ **NOT IMPLEMENTED** |
| **Complexity** | Low (style check) |

```cpp
auto p = std::make_unique<std::string>("hello");  // Unnecessary heap allocation
std::string s = "hello";  // Prefer stack allocation
```

---

# Summary: Complete Gap List

## Category A: Not Implemented (21 items)

### Type Safety (7)
1. **Type.1** - `reinterpret_cast` detection
2. **Type.2** - `static_cast` downcast detection
3. **Type.3** - `const_cast` detection
4. **Type.4** - C-style cast detection
5. **Type.6** - Uninitialized member detection
6. **Type.7** - Naked union access detection
7. **Type.8** - Varargs detection

### Bounds Safety (2)
8. **Bounds.3** - Array-to-pointer decay detection
9. **Bounds.4** - Unchecked subscript style warning

### Arithmetic Safety (3)
10. **Arithmetic.1** - Signed overflow detection
11. **Arithmetic.2** - Narrowing conversion detection
12. **Arithmetic.3** - Signed/unsigned mixing detection

### Initialization Safety (2)
13. **Init.1** - Vacuous initialization detection
14. **Type.5 gap** - Non-pointer uninitialized variables

### GSL Type Support (3)
15. **gsl::not_null** - Automatic nonnull inference
16. **gsl::owner** - Ownership tracking
17. **gsl::narrow** - Checked narrowing tracking

### Concurrency Safety (2)
18. **Data race detection**
19. **Deadlock detection**

### Resource Management (2)
20. **Memory leak detection** (new without delete)
21. **R.5** - Unnecessary heap allocation warning

## Category B: Partial Implementation (5 items)

1. **Type.5** - Uninitialized variables (pointers only)
2. **Bounds.1** - Pointer arithmetic (semantic, not style)
3. **Bounds.2** - Non-constant indices (allows symbolic proofs)
4. **Lifetime.3** - Dangling temporaries (some cases)
5. **R.1/R.3** - RAII/resource management (some types)

## Category C: Fully Implemented (6 items)

1. **Lifetime.1a** - Null pointer dereference
2. **Lifetime.1b** - Use-after-free
3. **Lifetime.1c** - Double-free
4. **Lifetime.1d** - Dangling pointers (scope-based)
5. **Lifetime.2** - Iterator invalidation
6. **Bounds verification** - Semantic bounds checking

---

# Implementation Priority Recommendations

## Tier 1: Quick Wins (Low effort, high value)

| Item | Effort | Value | Notes |
|------|--------|-------|-------|
| Type.1 `reinterpret_cast` | 1 day | High | Simple AST visitor |
| Type.3 `const_cast` | 1 day | High | Simple AST visitor |
| Type.4 C-style cast | 1 day | High | Simple AST visitor |
| Type.8 varargs | 1 day | Medium | Flag va_list usage |
| Bounds.3 array decay | 1 day | Medium | ImplicitCastExpr check |
| gsl::not_null | 1 day | High | Infer _nonnull=1 |

## Tier 2: Medium Effort (1-2 weeks each)

| Item | Effort | Value | Notes |
|------|--------|-------|-------|
| Type.2 static_cast downcast | 3 days | Medium | Check inheritance |
| Type.7 union tracking | 1 week | Medium | Track active member |
| Type.6 member init | 1 week | Medium | Constructor analysis |
| Arithmetic.2 narrowing | 1 week | High | Type width comparison |
| gsl::owner tracking | 1 week | Medium | Ownership dataflow |

## Tier 3: High Effort (Multi-week)

| Item | Effort | Value | Notes |
|------|--------|-------|-------|
| Type.5 full uninit | 2-4 weeks | High | Full dataflow analysis |
| Arithmetic.1 overflow | 2-4 weeks | High | Value range tracking |
| Memory leak detection | 2-4 weeks | High | Escape analysis |
| Lifetime.3 temporaries | 2-4 weeks | Medium | Complex lifetime |

## Tier 4: Research Projects

| Item | Effort | Value | Notes |
|------|--------|-------|-------|
| Data race detection | Months | Very High | Requires concurrency model |
| Deadlock detection | Months | Medium | Lock order analysis |

---

# References

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [P3081 R2: Core safety profiles for C++26](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3081r2.pdf)
- [P3402: Initialization Safety Profile](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3402r2.html)
- [GSL: Guidelines Support Library](https://github.com/microsoft/GSL)
- [clang-tidy cppcoreguidelines checks](https://clang.llvm.org/extra/clang-tidy/checks/list.html)
- [MSVC C++ Core Check](https://learn.microsoft.com/en-us/cpp/code-quality/code-analysis-for-cpp-corecheck)
- [Why Safety Profiles Failed (Sean Baxter)](https://www.circle-lang.org/draft-profiles.html)
- [Safe C++ Proposal](https://safecpp.org/P3390R0.html)
