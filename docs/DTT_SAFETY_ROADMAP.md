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
| **Resource Management** | Partial | Medium priority |
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

## Part 2: Type Safety Gaps

### Cast Safety (Type.1-4) — NOT IMPLEMENTED

These are **quick wins** (1 day each):

| Rule | What to Detect | Implementation |
|------|----------------|----------------|
| **Type.1** | `reinterpret_cast` | Visit `CXXReinterpretCastExpr` |
| **Type.2** | `static_cast` downcasts | Check inheritance relationship |
| **Type.3** | `const_cast` removing const | Visit `CXXConstCastExpr` |
| **Type.4** | C-style casts `(T)x` | Visit `CStyleCastExpr` |

```cpp
// All should be flagged:
void* p = get_ptr();
int* ip = reinterpret_cast<int*>(p);  // Type.1
int* ip = (int*)p;                     // Type.4

Base* b = get_base();
Derived* d = static_cast<Derived*>(b); // Type.2

const int* cp = &x;
int* mp = const_cast<int*>(cp);        // Type.3
```

### Initialization Safety (Type.5-6) — PARTIAL

**Current**: DTT tracks `_nonnull` for pointers only.

**Gap**: Non-pointer uninitialized variables not tracked.

```cpp
int x;
if (cond) x = 1;
return x;  // May be uninitialized - NOT DETECTED

int* p;
*p = 5;    // DTT catches this (ErrUninitializedPtr) ✓
```

**Enhancement**: Add `_initialized` property for all types, track through control flow.

### Union Safety (Type.7) — NOT IMPLEMENTED

**Current**: DTT tracks `std::variant` via `_variant_index`.

**Gap**: Raw `union` access not tracked.

```cpp
union U { int i; double d; };
U u;
u.i = 42;           // _active_member = "i"
double x = u.d;     // ERROR: reading inactive member
```

**Enhancement**: Add `_active_member` tracking for unions, similar to `_variant_index`.

### Varargs (Type.8) — NOT IMPLEMENTED

Flag all uses of `va_list`, `va_start`, `va_arg`, and `...` parameters.

---

## Part 3: Bounds Safety — Extend to Universal

### Current State

DTT semantically verifies bounds via `_size` tracking:

```cpp
std::vector<int> v(10);
v[5] = 1;   // OK: 5 < 10 ✓
v[10] = 1;  // ERROR: 10 >= 10 ✓
```

### Enhancement: Universal Bounds Tracking

Apply bounds tracking to ALL array-like access, not just STL containers:

#### A. C-Array Bounds

```cpp
int arr[10];
arr[9] = 1;    // OK: DTT tracks arr._size = 10
arr[10] = 1;   // ERROR: out of bounds
```

**Implementation**: On array declaration, set `_size` from array type.

#### B. Pointer Arithmetic Bounds

```cpp
int arr[10];
int* p = arr;       // p._size = 10, p._offset = 0
p[9] = 1;           // OK: offset 0 + index 9 < size 10

int* q = arr + 5;   // q._size = 5, q._offset = 5
q[4] = 1;           // OK: offset 5 + index 4 < original size 10
q[5] = 1;           // ERROR: offset 5 + index 5 >= 10
```

**New Properties**:
- `_offset`: Displacement from base pointer
- `_base`: Original array/allocation this pointer derives from

#### C. Function Parameter Bounds

```cpp
void process(int* data, size_t len) {
    // DTT learns: data._size = len
    data[len - 1] = 0;   // OK: len-1 < len (if len > 0)
    data[len] = 0;       // ERROR: len >= len
}
```

**Implementation**: Infer relationship between pointer and size parameters.

#### D. Loop Bounds Inference

```cpp
std::vector<int> v(10);
for (size_t i = 0; i < v.size(); i++) {
    v[i] = 0;  // OK: loop condition proves i < _size
}
```

**Implementation**: Extract proofs from loop conditions.

---

## Part 4: Arithmetic Safety — NEW CAPABILITY

### Narrowing Conversion Detection (Arithmetic.2)

**GSL Concept**: `gsl::narrow<T>()` throws if value doesn't fit.

**DTT Approach**: Detect narrowing at compile time using range tracking.

**New Properties**:
- `_min_value`: Proven lower bound
- `_max_value`: Proven upper bound

```cpp
int x = get_value();
if (x >= 0 && x <= 100) {
    // DTT knows: x._min_value = 0, x._max_value = 100
    char c = x;      // OK: fits in char [-128, 127]
}

int y = 300;
char c = y;          // ERROR: 300 > CHAR_MAX (127)

unsigned u = -1;     // ERROR: negative to unsigned
```

**Implementation**:
1. Track value ranges through conditionals
2. On narrowing conversion, verify source range fits in target type

### Signed Overflow Detection (Arithmetic.1)

```cpp
int a = INT_MAX;
int b = a + 1;       // ERROR: signed overflow is UB
```

**Implementation**: Use Z3 to prove arithmetic can overflow.

### Signed/Unsigned Mixing (Arithmetic.3)

```cpp
int i = -1;
unsigned u = 1;
if (i < u) { }       // WARNING: -1 converts to large unsigned
```

---

## Part 5: Lifetime Safety — Deep Enhancements

### Points-To Tracking (from Herb Sutter's Lifetime Profile)

**Key Concept**: Track what each pointer points to, and when it becomes invalid.

**New Properties**:
- `_points_to`: What this pointer references
- `_valid`: Whether the pointee is still valid

```cpp
std::vector<int> v = {1, 2, 3};
int* p = v.data();       // p._points_to = "v.buffer"
auto it = v.begin();     // it._points_to = "v.buffer"
int& ref = v[0];         // ref._points_to = "v.buffer"

v.push_back(4);          // May reallocate: INVALIDATE all pointing to "v.buffer"

*p = 10;                 // ERROR: p was invalidated
*it = 10;                // ERROR: it was invalidated
ref = 10;                // ERROR: ref was invalidated
```

**Invalidation Rules by Container**:

| Container | Invalidating Operations |
|-----------|------------------------|
| `vector` | `push_back`, `insert`, `emplace`, `reserve` (if capacity changes) |
| `deque` | `push_front`, `push_back`, `insert` |
| `string` | `+=`, `append`, `insert`, `resize` |
| `map/set` | Never (node-based, stable) |
| `list` | Never (node-based, stable) |

### Reference-Implies-Nonnull

A pointer derived from a reference is always non-null:

```cpp
void process(int& ref) {
    int* p = &ref;   // p._nonnull = 1 (references can't be null)
    *p = 10;         // No null check needed
}
```

### Constructor Guarantee Inference

Infer invariants from constructor implementation:

```cpp
class Widget {
    int* data;
public:
    Widget() : data(new int(0)) {}  // data always non-null after construction
};

Widget w;
*w.data;  // OK: constructor establishes data._nonnull = 1
```

---

## Part 6: Resource Safety — Ownership Tracking

### The `owner` Concept

**GSL Concept**: `gsl::owner<T*>` marks pointers that own memory.

**DTT Approach**: Infer ownership from allocation patterns.

**New Properties**:
- `_owns_resource`: This pointer owns heap memory
- `_freed`: Memory has been deallocated

### Leak Detection

```cpp
void leak() {
    int* p = new int(42);  // p._owns_resource = 1, p._freed = 0
    return;                // ERROR: owned resource not freed
}
```

### Double-Free (Already Implemented)

```cpp
int* p = new int(42);
delete p;              // p._freed = 1
delete p;              // ERROR: _freed already 1
```

### Ownership Transfer

```cpp
int* create() {
    int* p = new int(42);   // p._owns_resource = 1
    return p;               // Ownership transfers to caller
}

void consume(int* p) {
    delete p;               // p._owns_resource should be 1
}

int* p = create();          // p._owns_resource = 1
consume(p);                 // Ownership transfers to consume()
// p is now invalid
```

---

## Part 7: Contract Propagation

### Implicit Precondition Inference

Infer preconditions from function body:

```cpp
void process(std::vector<int>& v) {
    int x = v.front();   // Implies: requires v._size > 0
}

// At call site:
std::vector<int> data;
process(data);           // ERROR: _size == 0, but requires > 0
```

### Postcondition Inference (Already Implemented)

DTT already infers postconditions from method implementations:

```cpp
void ensure_capacity(std::vector<int>& v, size_t n) {
    v.reserve(n);        // DTT infers: ensures v._capacity >= n
}
```

### Consume Semantics (Move Invalidation)

**GSL Concept**: F.18 — For "consume" parameters, pass by `X&&`.

```cpp
void consume(std::vector<int>&& v);  // Consume semantic

std::vector<int> data = {1, 2, 3};
consume(std::move(data));
data[0] = 10;            // ERROR: use after consume
```

---

## Part 8: Type State Tracking

### Generalized State Machines

Track object states and enforce state-dependent operations:

```cpp
// FILE* states: CLOSED -> OPEN -> CLOSED
FILE* f = fopen("x.txt", "r");  // state: OPEN
fread(buf, 1, 10, f);           // OK: requires OPEN
fclose(f);                       // state: CLOSED
fread(buf, 1, 10, f);           // ERROR: requires OPEN, but CLOSED
```

**New Property**: `_state` (enum-like)

### Connection Protocol Example

```cpp
Connection c;                    // state: DISCONNECTED
c.send(data);                   // ERROR: requires CONNECTED
c.connect("server");            // state: CONNECTED
c.send(data);                   // OK
c.disconnect();                 // state: DISCONNECTED
```

---

## Part 9: Taint Tracking (Future)

### Untrusted Input Detection

Mark data from untrusted sources and require validation:

**New Properties**:
- `_tainted`: Data from untrusted source
- `_validated`: Data has been validated

```cpp
size_t len = read_from_network();  // len._tainted = 1
char buf[100];
memcpy(buf, data, len);            // ERROR: using tainted length

if (len <= 100) {                  // Validates range
    memcpy(buf, data, len);        // OK: len._validated = 1
}
```

**Taint Sources**: Network input, file input, user input, environment variables

**Taint Sinks**: Array indices, allocation sizes, loop bounds

---

## Part 10: New Properties Summary

| Property | Type | Tracks | Used For |
|----------|------|--------|----------|
| `_size` | int | Container/array size | Bounds checking |
| `_offset` | int | Pointer displacement | Pointer arithmetic |
| `_nonnull` | bool | Pointer validity | Null deref |
| `_freed` | bool | Deallocation state | Use-after-free, double-free |
| `_owns_resource` | bool | Memory ownership | Leak detection |
| `_points_to` | string | Pointee identity | Iterator invalidation |
| `_valid` | bool | Pointee still exists | Dangling detection |
| `_min_value` | int | Numeric lower bound | Narrowing detection |
| `_max_value` | int | Numeric upper bound | Narrowing detection |
| `_initialized` | bool | Variable initialized | Uninit detection |
| `_active_member` | string | Union active field | Union safety |
| `_state` | enum | Object protocol state | Type state |
| `_tainted` | bool | Untrusted data | Input validation |

---

## Part 11: Implementation Roadmap

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
| Full uninitialized detection | 3 weeks | High | `_initialized` for all types |
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

## Part 12: Priority Matrix

| Feature | Complexity | Impact | Priority |
|---------|------------|--------|----------|
| Cast detection (Type.1-4) | Low | High | **P0** |
| C-array bounds | Low | High | **P0** |
| Narrowing detection | Medium | High | **P0** |
| Pointer arithmetic bounds | Medium | Very High | **P1** |
| Iterator invalidation (points-to) | High | Very High | **P1** |
| Union tracking | Medium | Medium | **P1** |
| Ownership/leak detection | High | High | **P1** |
| Full uninitialized | High | High | **P2** |
| Overflow detection | High | High | **P2** |
| Type state | Medium | Medium | **P2** |
| Taint tracking | High | High | **P3** |
| Concurrency | Very High | Very High | **P4** |

---

## Part 13: DTT vs GSL Philosophy

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
