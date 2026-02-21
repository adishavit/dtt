# DTT User Guide

Detailed documentation for the DTT (Dependent Type Tracking) Clang plugin.

## Table of Contents

- [Features](#features)
- [Diagnostic Modes](#diagnostic-modes)
- [Annotation Reference](#annotation-reference)
- [Constraint Operators](#constraint-operators)
- [Method Call Tracking](#method-call-tracking)
- [Automatic STL Tracking](#automatic-stl-tracking)
- [Index Bounds Checking](#index-bounds-checking)
- [Proof System](#proof-system)
- [Assignment & Swap Tracking](#assignment--swap-tracking)
- [Reference Invalidation](#reference-invalidation)
- [Standard Library Support](#standard-library-support)
- [C Library Support](#c-library-support)
  - [Automatic Resource Leak Detection](#automatic-resource-leak-detection)
  - [FILE* Null Tracking](#file-null-tracking)
  - [Use-After-Free Detection](#use-after-free-detection)
- [Buffer Pointer Tracking](#buffer-pointer-tracking)
- [Uninitialized Memory Detection](#uninitialized-memory-detection)
- [Automatic Annotations](#automatic-annotations)
- [Value Concepts](#value-concepts)
- [Advanced Analysis Features](#advanced-analysis-features)
- [Known Limitations](#known-limitations)
- [Version History](#version-history)

## Features

| Feature | Description |
|---------|-------------|
| **Field Tracking** | Track any `_field` property through code flow |
| **Struct Paths** | Full path tracking: `s.v.len`, `obj.inner._size` |
| **Compound Constraints** | AND (`&&`) and OR (`\|\|`) in constraints |
| **Arithmetic Expressions** | `+`, `-`, `*`, `/` in constraints |
| **Comparison Operators** | `>`, `>=`, `<`, `<=`, `==`, `!=` |
| **Runtime Guards** | `if (x > 0)` proves `x > 0` in that branch |
| **Else-Branch Guards** | `else` branch gets negated guard |
| **Early Exit Proofs** | `if (cond) { return; }` proves `!cond` after |
| **Nested Guards** | Guards compose through nested `if` statements |
| **Postconditions** | `ensures=` sets property after call |
| **Preconditions** | `requires=` checks property before call |
| **old() References** | `old(_size)` captures value before call |
| **Boolean Returns** | `ensures_if_true/false` for boolean methods |
| **Return Tracking** | `returns=_field` tracks return value |
| **Multi-Arg Constraints** | `arg0.len < arg1.len` |
| **Argument Tracking** | `ensures=_size == arg0` |
| **Transitive Proofs** | `_size == n` + `n > 0` → `_size > 0` |
| **Proof Persistence** | Proofs survive across statements |
| **Proof Copying** | Proofs copy with variable assignment |
| **Proof Invalidation** | Modifying field clears related proofs |
| **Switch Totality** | `dtt:total` marks exhaustive switches |
| **Index Arithmetic** | `v[i + 1]`, `v[n - 1]` symbolic evaluation |
| **Loop Bounds** | For-loop: `!=` condition, pre-declared var, increment direction verification |
| **Copy/Move Construction** | `Vec v2(v1)` and `Vec v2(std::move(v1))` tracked |
| **Copy/Move Assignment** | Properties transfer on `v2 = v1` |
| **Chain Assignment** | `v3 = v2 = v1` properly tracked |
| **Move Invalidation** | `std::move(v)` invalidates source |
| **Swap Tracking** | `swap()` and `std::swap()` exchange properties |
| **Self-Assignment** | `v = v` safely ignored |
| **Template Parameters** | `_tparam0`, `_tparam1` extraction |
| **Z3 SMT Solver** | Complex symbolic constraint solving |
| **STL Containers** | Auto-tracking: vector, deque, list, set, map, stack, queue, etc. |
| **std::string** | Size tracking and bounds checking |
| **std::array\<T,N\>** | Automatic `_size = N` tracking |
| **std::span\<T,N\>** | Static extent bounds checking |
| **std::bitset\<N\>** | Bit index bounds checking |
| **std::optional** | `_has_value` tracking, safe `value()` access |
| **std::unique_ptr/shared_ptr** | `_nonnull` tracking, safe dereference |
| **FILE* Tracking** | Null checks for fopen/fclose/fread/fwrite |
| **C String Functions** | Use-after-free detection for strcpy, strlen, memcpy, etc. |
| **Reference Invalidation** | Unknown functions invalidate ref args |
| **Lambda Isolation** | Lambda bodies don't affect outer scope |
| **Function Scope** | Variables isolated per function |
| **Diagnostic Modes** | `--dtt-mode=inform\|enforce\|quiet` |
| **Lost Tracking Warnings** | Category C: warns on first unsafe use after tracking lost |
| **Annotation Conflicts** | Category G: errors on conflicting annotations |
| **Buffer Pointer Tracking** | Track bounds for raw pointers from arrays: `int* p = arr;` |
| **Cast Tracking** | All cast types preserve bounds: reinterpret_cast, C-style, static_cast via void* |
| **Type Range Inference** | `unsigned char` index on `[256+]` array proven safe without Z3 |
| **Guard-based Buffer Bounds** | `if (idx < N) { arr[idx]; }` proven safe via Z3 |
| **Negative Index Detection** | `v[-1]` rejected regardless of size |
| **argc Auto-Annotation** | `main(int argc, ...)` gets `argc >= 1` automatically |
| **Transitive Preconditions** | Preconditions inferred in callees propagate to callers |
| **Frame Inference** | Tracks field writes per function; unwritten fields are "framed" |
| **Counterexamples** | "Cannot verify" messages include concrete failing values |
| **Provenance Chains** | Error notes trace full state history: constructor, push_back, clear, ... |
| **Fix Suggestions** | IDE-actionable FixItHints for common patterns (empty check, null check, bounds check) |
| **Conditional Annotations** | `when` guards in `.dtt` files for conditional postconditions/scope_exit |
| **Libc Null Safety** | Null-pointer checking for 30+ C string, conversion, and time functions |
| **Generalized Fixpoint** | Datalog-style transitive propagation of all property constraints across call chains |
| **Backward Search** | Dynamic annotation registry search for computed fix suggestions on any type |
| **Uninitialized Memory** | Detects uninit scalars, pointers, POD struct members passed to functions |
| **Variadics & Packs** | Tracks uninit values through C varargs, variadic templates, parameter packs, and fold expressions |

## Diagnostic Modes

DTT provides three modes that control how it handles situations where tracking is lost or constraints can't be resolved:

| Mode | Lost Tracking (C) | Undecidable (D) | Proven Bug (F) | Annotation Error (G) |
|------|-------------------|-----------------|----------------|----------------------|
| `--dtt-mode=inform` (default) | Warning | Warning | Error | Error |
| `--dtt-mode=enforce` | Error | Error | Error | Error |
| `--dtt-mode=quiet` | Silent | Silent | Error | Error |

### Philosophy

DTT is a **bug-finder by default**: it blocks only proven violations and annotation errors. Coverage gaps and solver failures are reported as warnings so teams can choose their strictness.

### Usage

```bash
# Default: warnings for coverage gaps
clang++ -fplugin=DTTPlugin.so file.cpp

# Strict: errors for any uncertainty
clang++ -fplugin=DTTPlugin.so -Xclang -plugin-arg-dtt -Xclang --dtt-mode=enforce file.cpp

# Quiet: only error on proven bugs
clang++ -fplugin=DTTPlugin.so -Xclang -plugin-arg-dtt -Xclang --dtt-mode=quiet file.cpp
```

### Fine-grained Control

Use standard clang flags to control specific diagnostics:

```bash
# Promote Category C (lost tracking) to error
clang++ -fplugin=DTTPlugin.so -Werror=dtt-lost-tracking file.cpp

# Silence Category D (solver failures)
clang++ -fplugin=DTTPlugin.so -Wno-dtt-solver-failed file.cpp
```

### Category C: Lost Tracking

When tracking is lost (e.g., passing to an unannotated function), DTT:
1. Records the location and reason
2. Waits until first unsafe use
3. Emits warning with note pointing back to loss site
4. Warns once per (variable, field) combination

```cpp
void unknown(Vec& v);  // Unannotated

void test() {
    Vec v;
    v._size = 10;
    unknown(v);     // Tracking lost here
    v.front();      // warning: cannot verify constraint (tracking was lost)
                    // note: tracking was lost here: call to unannotated function 'unknown()'
}
```

### Provenance Chains

When DTT reports a precondition violation, it emits the **complete history** of the tracked property as `note:` diagnostics. This tells you exactly how the value evolved to reach the failing state:

```
error: DTT: vector::front requires _size > 0, but v._size = 0
   20 |     int x = v.front();
      |             ^
note: v._size set to 0 (vector constructor)
   17 |     std::vector<int> v;
      |                      ^
note: v._size set to 1 (vector::push_back)
   18 |     v.push_back(1);
      |     ^
note: v._size set to 0 (vector::clear)
   19 |     v.clear();
      |     ^
```

Provenance is tracked for all properties set through `.dtt` annotations (constructor postconditions, method postconditions). Each entry records the source location, the operation, and the resulting value.

### Fix Suggestions (FixItHint)

For common precondition patterns, DTT emits concrete fix suggestions that IDEs can apply with one click:

| Failed Precondition | Suggested Guard |
|---------------------|-----------------|
| `_size > 0` | `if (!obj.empty()) { ... }` |
| `_has_value != 0` | `if (opt.has_value()) { ... }` |
| `_nonnull != 0` | `if (ptr) { ... }` |
| `arg0 < _size` | `if (idx < obj.size()) { ... }` |

The suggestion appears as both a human-readable `note:` and a machine-readable `fix-it:` insertion that tools like clang-tidy, VS Code, and CLion can apply automatically.

### Backward Search (Computed Fix Suggestions)

Beyond the pattern table above, DTT performs a **backward search** over the annotation registry to discover ALL operations on the object's type that could establish the required property. This is not a hardcoded list — it dynamically queries every registered postcondition:

```
error: DTT: vector::front requires _size > 0, but v._size = 0
   21 |     int x = v.front();
      |             ^
note: add guard: if (!v.empty()) { ... }
note: could establish _size: assign() with arg0 > 0, emplace(), emplace_back(),
      resize() with arg0 > 0, push_back()
```

For optional types:
```
error: DTT: optional::value requires _has_value == 1, but opt._has_value = 0
note: could establish _has_value: emplace(), operator=()
note: could guard with: if (obj.has_value()) { ... }
```

**How it works:** When a precondition fails, DTT searches `MethodAnnotations` for:
1. **Postconditions** (`ensures`) that set the required field to a satisfying value
2. **Boolean guards** (`ensures_if_true`/`ensures_if_false`) that prove the required property

This works for ANY annotated type — including user-defined types with custom `.dtt` files. If you add a custom type with a `fill(n)` method that `ensures _size = arg0`, backward search will automatically suggest `fill() with arg0 > 0` when `_size > 0` fails.

### Category G: Annotation Conflicts

Conflicting annotations are always errors regardless of mode:

```cpp
struct Vec {
    // ERROR: Conflicting ensures
    [[clang::annotate("dtt:ensures=_size == 0")]]
    [[clang::annotate("dtt:ensures=_size == 10")]]
    void confuse();
};
```

## Annotation Reference

### Preconditions (`requires`)

```cpp
[[clang::annotate("dtt:requires=_size > 0")]]
void pop();

[[clang::annotate("dtt:requires=arg0 < _size")]]
T& operator[](size_t i);

[[clang::annotate("dtt:requires=arg0 + arg1 <= _size")]]
void copy(int start, int len);

[[clang::annotate("dtt:requires=arg0 * arg1 < _capacity")]]
void allocate(int rows, int cols);

[[clang::annotate("dtt:requires=arg0 - 1 < _size")]]
T& at_one_based(size_t i);  // 1-based indexing
```

### Postconditions (`ensures`)

```cpp
[[clang::annotate("dtt:ensures=_size == 0")]]
void clear();

[[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
void push();

[[clang::annotate("dtt:ensures=_size == old(_size) - 1")]]
void pop();

[[clang::annotate("dtt:ensures=_size == arg0")]]
void resize(size_t n);
```

### Return Value Tracking

```cpp
[[clang::annotate("dtt:returns=_size")]]
size_t size() const;

[[clang::annotate("dtt:ensures_if_true=_size == 0")]]
[[clang::annotate("dtt:ensures_if_false=_size > 0")]]
bool empty() const;
```

### Conditional Postconditions (`when` guards)

In `.dtt` annotation files, postconditions and scope_exit checks can be made conditional with a `when` guard. The postcondition is only applied when the guard expression holds in the current proof state:

```
fn fopen(2) {
  ensures result.@open = 1 when result._nonnull != 0
  scope_exit result.@open == 0 when result._nonnull != 0
}
```

**Supported guard contexts:**

| Context | Syntax | Example |
|---------|--------|---------|
| Free function ensures | `ensures <expr> when <guard>` | `ensures result.@open = 1 when result._nonnull != 0` |
| Free function scope_exit | `scope_exit <expr> when <guard>` | `scope_exit result.@open == 0 when result._nonnull != 0` |
| Type/trait method ensures | `ensures <expr> when <guard>` | `ensures _size = old(_size) + 1 when _ready != 0` |

**Why it matters:** Functions that can fail (fopen, malloc, connect) return nullable results. Without guards, an `ensures result.@open = 1` unconditionally sets the ghost field — leading to false positives when the result is null and the resource was never acquired. With `when result._nonnull != 0`, the postcondition only fires when the null check succeeds.

**Guard evaluation:** Guards are evaluated against the current proof state at the point where the postcondition would be applied. If the guard expression cannot be proven true, the postcondition is silently skipped.

### Switch Totality

```cpp
[[clang::annotate("dtt:total")]]
int process(State s);  // Compiler checks all cases covered
```

## Constraint Operators

| Operator | Example | Description |
|----------|---------|-------------|
| `>` | `_size > 0` | Greater than |
| `>=` | `_size >= 1` | Greater or equal |
| `<` | `arg0 < _size` | Less than |
| `<=` | `arg0 + arg1 <= _size` | Less or equal |
| `==` | `_size == 0` | Equal |
| `!=` | `_size != 0` | Not equal |
| `&&` | `_size > 0 && _cap > 0` | Logical AND |
| `\|\|` | `_size == 0 \|\| _valid` | Logical OR |
| `+` | `arg0 + arg1` | Addition |
| `-` | `old(_size) - 1` | Subtraction |
| `*` | `arg0 * arg1` | Multiplication |
| `/` | `_size / 2` | Division |
| `old()` | `old(_size)` | Value before call |

## Property Prefixes

DTT supports two property prefix conventions:

### `_` Prefix — Runtime Fields

Properties starting with `_` typically correspond to actual struct members:

```cpp
struct Vec {
    int _size = 0;  // Real runtime field

    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push(int x);
};
```

**Caveat:** Users can bypass annotations by writing directly to the field:
```cpp
v._size = 100;  // Compiles! Bypasses state tracking
```

### `@` Prefix — Ghost State (Unforgeable)

Properties starting with `@` exist only in compile-time analysis — no runtime storage:

```cpp
struct SecureData {
    std::vector<char> payload;  // No @encrypted field exists!

    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt();

    [[clang::annotate("dtt:requires=@encrypted == 1")]]
    void send();
};
```

**Key benefit:** `@` is invalid in C++ identifiers, so users cannot forge the state:
```cpp
s.@encrypted = 1;  // SYNTAX ERROR — impossible to bypass
```

**When to use which:**
| Prefix | Use Case | Runtime Storage | Forgeable? |
|--------|----------|-----------------|------------|
| `_` | Properties mirroring actual fields (`_size`, `_capacity`) | Yes | Yes |
| `@` | Pure verification state (`@encrypted`, `@authenticated`) | No | No |

## Method Call Tracking

```cpp
struct Container {
    [[clang::annotate("dtt:returns=_size")]]
    int size() const;

    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push();

    [[clang::annotate("dtt:requires=_size > 0")]]
    [[clang::annotate("dtt:ensures=_size == old(_size) - 1")]]
    void pop();

    [[clang::annotate("dtt:ensures_if_true=_size == 0")]]
    [[clang::annotate("dtt:ensures_if_false=_size > 0")]]
    bool empty() const;
};

Container c;
c.push();     // _size = 1
c.push();     // _size = 2

if (!c.empty()) {
    c.pop();  // OK: !empty() proves _size > 0
}

c.pop();
c.pop();
c.pop();      // ERROR: _size = 0, requires > 0
```

## Automatic STL Tracking

DTT automatically tracks standard library types — no wrappers or special includes needed.

### Sequence Containers

**std::vector, std::deque, std::list** — full size tracking with bounds checking:

```cpp
std::vector<int> v;
v.push_back(1);     // _size = 1
v.resize(10);       // _size = 10
v[9] = 42;          // OK: 9 < 10

if (!v.empty()) {
    v.pop_back();   // OK: !empty() proves _size > 0
}

v.clear();
v.front();          // ERROR: _size = 0
```

| Method | Precondition | Postcondition |
|--------|--------------|---------------|
| `size()` | - | returns `_size` |
| `empty()` | - | true iff `_size == 0` |
| `push_back()`, `emplace_back()` | - | `_size = old(_size) + 1` |
| `push_front()`, `emplace_front()` | - | `_size = old(_size) + 1` |
| `pop_back()` | `_size > 0` | `_size = old(_size) - 1` |
| `pop_front()` | `_size > 0` | `_size = old(_size) - 1` |
| `front()`, `back()` | `_size > 0` | - |
| `clear()` | - | `_size = 0` |
| `resize(n)` | - | `_size = n` |
| `assign(n, x)` | - | `_size = n` |
| `insert(pos, val)` | - | `_size = old(_size) + 1` |
| `insert(pos, n, val)` | - | `_size = old(_size) + n` |
| `emplace(pos, ...)` | - | `_size = old(_size) + 1` |
| `erase(pos)` | `_size > 0` | `_size = old(_size) - 1` |
| `operator[](i)` | `i < _size` | - |

**std::forward_list** — size tracking (no `size()` method, but `empty()` and mutations tracked).

### Strings

**std::string, std::wstring** (via `std::basic_string`):

```cpp
std::string s;          // _size = 0
s.push_back('a');       // _size = 1
s += 'b';               // _size = 2
s.resize(10);           // _size = 10
s[9] = 'x';             // OK: 9 < 10
s[10] = 'y';            // ERROR: 10 >= 10
```

| Method | Precondition | Postcondition |
|--------|--------------|---------------|
| `size()`, `length()` | - | returns `_size` |
| `empty()` | - | true iff `_size == 0` |
| `push_back()`, `operator+=` | - | `_size = old(_size) + 1` |
| `pop_back()` | `_size > 0` | `_size = old(_size) - 1` |
| `front()`, `back()` | `_size > 0` | - |
| `clear()` | - | `_size = 0` |
| `resize(n)` | - | `_size = n` |
| `append(n, ch)` | - | `_size = old(_size) + n` |
| `operator[](i)` | `i < _size` | - |

**std::string_view** (via `std::basic_string_view`):

```cpp
std::string_view sv("hello", 5);  // _size = 5
sv[4];                            // OK
sv[5];                            // ERROR
sv.remove_prefix(2);              // _size = 3
```

### Associative Containers

**std::set, std::multiset, std::map, std::multimap** and their unordered variants — size tracking:

```cpp
std::set<int> s;        // _size = 0
s.insert(42);           // _size = 1
s.insert(17);           // _size = 2
s.clear();              // _size = 0
```

| Method | Precondition | Postcondition |
|--------|--------------|---------------|
| `size()` | - | returns `_size` |
| `empty()` | - | true iff `_size == 0` |
| `insert(val)` | - | `_size = old(_size) + 1` |
| `clear()` | - | `_size = 0` |
| `at(key)` | `_size > 0` | - |

### Container Adapters

**std::stack, std::queue, std::priority_queue** — size tracking:

```cpp
std::stack<int> stk;    // _size = 0
stk.push(1);            // _size = 1
stk.push(2);            // _size = 2
stk.top();              // OK: _size > 0
stk.pop();              // _size = 1
stk.pop();              // _size = 0
stk.top();              // ERROR: _size = 0
```

| Method | Precondition | Postcondition |
|--------|--------------|---------------|
| `size()` | - | returns `_size` |
| `empty()` | - | true iff `_size == 0` |
| `push()`, `emplace()` | - | `_size = old(_size) + 1` |
| `pop()` | `_size > 0` | - |
| `top()` | `_size > 0` | - |
| `front()`, `back()` | `_size > 0` | - |

## Index Bounds Checking

```cpp
std::vector<int> v;
v.push_back(1);
v.push_back(2);  // _size = 2

v[0] = 10;  // OK: 0 < 2
v[1] = 20;  // OK: 1 < 2
v[2] = 30;  // ERROR: 2 < 2 is false
```

**Negative indices:**
```cpp
std::vector<int> v;
v.resize(5);
v[-1];      // ERROR: negative index -1 is always out of bounds
v[-2];      // ERROR: negative index -2 is always out of bounds
```

Negative indices are always rejected regardless of container size, since array indices are unsigned.

**Index arithmetic:**
```cpp
v.resize(10);
for (int i = 0; i < 9; i++) {
    v[i + 1];  // OK: i < 9, so i + 1 < 10
}
```

**2D array access:**
```cpp
std::array<std::array<int, 4>, 3> arr;
arr[2][3];  // OK: 2 < 3 and 3 < 4
arr[3][0];  // ERROR: 3 >= 3
```

## Proof System

### Runtime Guards

```cpp
void process(int n) {
    std::vector<int> v;
    v.resize(n);  // _size = n (symbolic)

    if (n > 0) {
        v.front();  // OK: guard proves n > 0, _size == n
    }
}
```

### Guard-based Buffer Bounds

Guards also prove buffer access safety for raw arrays:

```cpp
static const int table[8] = {0, 1, 2, 3, 4, 5, 6, 7};

void lookup(unsigned offset) {
    if (offset < 8) {
        int val = table[offset];  // OK: guard proves offset < 8
    }
}
```

All comparison forms work:

```cpp
if (i < 8)    { arr[i]; }  // OK: i < 8
if (8 > i)    { arr[i]; }  // OK: reversed form
if (!(i >= 8)) { arr[i]; }  // OK: negated form
if (i >= 8) return;
arr[i];                     // OK: early return proves i < 8
```

### Nested Guards

```cpp
if (x > 0) {
    if (y > 0) {
        // Both x > 0 and y > 0 proven here
    }
}
```

### Else-Branch Guards

```cpp
if (v.empty()) {
    // _size == 0 proven
} else {
    v.front();  // OK: !empty() proves _size > 0
}
```

### Early Exit Proof Propagation

When an `if` block terminates with `return`, `throw`, `break`, or `continue`, the negation of the condition holds for the rest of the scope:

```cpp
void process(Vec& v) {
    if (v._size <= 0) {
        return;  // Exit early if empty
    }
    // Here: _size > 0 (negation of <= 0)
    v.front();  // OK
}

void validate(Vec& v) {
    if (v._size <= 0) {
        throw std::runtime_error("empty");
    }
    v.front();  // OK: throw proves _size > 0
}

// Works with break/continue in loops
for (auto& v : vecs) {
    if (v._size <= 0) {
        continue;
    }
    v.front();  // OK: _size > 0
}
```

### Transitive Proofs

```cpp
v.resize(n);  // _size = n
if (n > 0) {
    // n > 0 proven
    // Transitive: _size == n, so _size > 0
    v.front();  // OK
}
```

### Proof Invalidation

```cpp
v.resize(5);   // _size = 5
// proof: _size == 5
v.push_back(1);  // Invalidates _size proof
// Now _size = 6, old proofs cleared
```

## Construction, Assignment & Swap Tracking

```cpp
std::vector<int> v1, v2;
v1.resize(5);   // v1._size = 5
v2.resize(10);  // v2._size = 10

// Copy construction
std::vector<int> v3(v1);      // v3._size = 5

// Move construction
std::vector<int> v4(std::move(v2));  // v4._size = 10, v2 invalidated
v2.front();                          // ERROR: v2 moved-from

// Copy assignment
v3 = v1;        // v3._size = 5

// Move assignment
v3 = std::move(v1);  // v3._size = 5, v1 invalidated
v1.front();          // ERROR: v1 moved-from

// Chain assignment
v3 = v2 = v1;        // All get v1's properties

// Swap
v1.swap(v2);         // Properties exchanged
std::swap(v1, v2);   // Also works

// Self-assignment (safe)
v1 = v1;             // No-op
```

## Reference Invalidation

DTT uses **conservative invalidation** when tracked objects are passed to unknown functions:

### Rules

| Scenario | Behavior |
|----------|----------|
| Pass by value | No invalidation (copy) |
| Const reference (`const T&`) | No invalidation (can't modify) |
| Const pointer (`const T*`) | No invalidation (can't modify) |
| Const rvalue ref (`const T&&`) | No invalidation (can't modify) |
| Non-const ref to **annotated** function | Trust annotations |
| Non-const ref to **unannotated** function | **Invalidate** |
| Non-const pointer to **unannotated** function | **Invalidate** |
| Rvalue ref (`T&&`) to **unannotated** function | **Invalidate** |
| `std::move(x)` argument | **Invalidate `x`** |
| Non-const method with annotations | Trust annotations |
| Non-const method without annotations | **Invalidate** |

### Examples

```cpp
void unknown(Vec& v);  // No DTT annotations

void test() {
    Vec v;
    v.len = 10;

    unknown(v);     // Invalidates v.len → UNKNOWN

    needsTen(v);    // ERROR: v.len is unknown
}
```

**Re-assignment restores tracking:**
```cpp
unknown(v);      // v.len = UNKNOWN
v.len = 20;      // v.len = 20 (tracked again)
needsTen(v);     // OK: v.len = 20
```

**Annotated functions are trusted:**
```cpp
[[clang::annotate("dtt:requires=len > 0")]]
void checkPositive(Vec& v);  // Has annotation

Vec v;
v.len = 10;
checkPositive(v);  // NOT invalidated (has dtt: annotation)
needsTen(v);       // OK: v.len still = 10
```

**Const refs and pointers are safe:**
```cpp
void readOnly(const Vec& v);
void readOnlyPtr(const Vec* v);

Vec v;
v.len = 10;
readOnly(v);       // Not invalidated
readOnlyPtr(&v);   // Not invalidated
needsTen(v);       // OK: v.len still = 10
```

**Non-const pointers invalidate:**
```cpp
void maybeModify(Vec* v);  // Non-const pointer, no annotations

Vec v;
v.len = 10;
maybeModify(&v);   // Invalidates v.len
needsTen(v);       // ERROR: v.len unknown
```

**Rvalue references (T&&) invalidate:**
```cpp
void consume(Vec&& v);  // Rvalue ref, no annotations

Vec v;
v.len = 10;
consume(std::move(v));  // Invalidates v.len
needsTen(v);            // ERROR: v.len unknown (moved-from)
```

**std::move() traces to the original object:**
```cpp
void sink(Vec&& v);

Vec v;
v.len = 10;
sink(std::move(v));  // std::move(v) → invalidates v, not a temp
needsTen(v);         // ERROR: v was moved-from
```

**Non-const methods without annotations:**
```cpp
struct Vec {
    void mutate();        // Non-const, no annotations → invalidates
    int getLen() const;   // Const → safe
};

Vec v;
v.len = 10;
v.mutate();      // Invalidates v.len
needsTen(v);     // ERROR: v.len unknown
```

### Lambda Isolation

Lambda bodies are analyzed at call time, not definition time:

```cpp
Vec v;
v.len = 10;

auto lambda = [&v]() { v.len = 0; };  // Body NOT analyzed yet

needsNonEmpty(v);  // OK: v.len still = 10

// Note: Lambda call tracking not yet implemented
```

## Standard Library Support

### Fixed-Size Containers

DTT extracts template parameters for bounds checking:

```cpp
// std::array<T, N> — _size = N
std::array<int, 10> arr;
arr[9];   // OK
arr[10];  // ERROR

// std::bitset<N> — _size = N
std::bitset<8> bits;
bits[7];  // OK
bits[8];  // ERROR

// std::span<T, N> — _size = N (static extent)
std::span<int, 5> sp(data);
sp[4];    // OK
sp[5];    // ERROR
```

### Optional Types

**std::optional** — tracks `_has_value`:

```cpp
std::optional<int> opt;      // _has_value = 0
opt.value();                 // ERROR: requires _has_value == 1

opt = 42;                    // _has_value = 1
opt.value();                 // OK
*opt = 10;                   // OK

if (opt.has_value()) {
    opt.value();             // OK: guard proves _has_value == 1
}

if (opt) {
    *opt;                    // OK: operator bool proves _has_value == 1
}

opt.reset();                 // _has_value = 0
opt.emplace(99);             // _has_value = 1
```

| Method | Precondition | Postcondition |
|--------|--------------|---------------|
| `value()` | `_has_value == 1` | - |
| `operator*`, `operator->` | `_has_value == 1` | - |
| `has_value()` | - | true iff `_has_value == 1` |
| `operator bool` | - | true iff `_has_value == 1` |
| `reset()` | - | `_has_value = 0` |
| `emplace(...)` | - | `_has_value = 1` |
| `operator=(val)` | - | `_has_value = 1` |

**std::variant** — tracks `_variant_index` (which alternative is active):

```cpp
std::variant<int, std::string> v = 42;    // _variant_index = 0
std::get<int>(v);                          // OK: index 0 holds int
std::get<std::string>(v);                  // ERROR: _variant_index != 1

v = "hello";                               // _variant_index = 1
std::get<std::string>(v);                  // OK: index 1 holds string

v.emplace<0>(100);                         // _variant_index = 0
```

**Type guards with `holds_alternative`:**

```cpp
std::variant<int, double> v = 3.14;

if (std::holds_alternative<double>(v)) {
    std::get<double>(v);    // OK: proved by guard
} else {
    std::get<double>(v);    // ERROR: proved NOT double in else branch
}
```

**Safe access with `get_if`:**

```cpp
std::variant<int, std::string> v = 42;
if (auto* p = std::get_if<int>(&v)) {
    *p = 100;               // OK: p is non-null when type matches
}
```

| Method | Precondition | Postcondition |
|--------|--------------|---------------|
| `std::get<I>(v)` | `_variant_index == I` | - |
| `std::get<T>(v)` | `_variant_index == index_of<T>` | - |
| `std::holds_alternative<T>(v)` | - | Proof: `_variant_index == index_of<T>` |
| `std::get_if<T>(&v)` | - | Returns non-null iff type matches |
| `v.emplace<I>(...)` | - | `_variant_index = I` |
| `v.emplace<T>(...)` | - | `_variant_index = index_of<T>` |
| `v = value` | - | `_variant_index` = deduced from value type |

**std::any** — tracks `_has_value` and `_any_type` (the contained type):

```cpp
std::any a;                              // _has_value = 0
std::any_cast<int>(a);                   // ERROR: _has_value != 1

a = 42;                                  // _has_value = 1, _any_type = int
std::any_cast<int>(a);                   // OK: type matches
std::any_cast<double>(a);                // ERROR: _any_type != double

a.reset();                               // _has_value = 0
```

**Type guards with `a.type() == typeid(T)`:**

```cpp
std::any a = 42;

if (a.type() == typeid(int)) {
    std::any_cast<int>(a);     // OK: proved by guard
} else {
    std::any_cast<int>(a);     // ERROR: proved NOT int in else branch
}
```

| Method | Precondition | Postcondition |
|--------|--------------|---------------|
| `std::any_cast<T>(a)` | `_has_value == 1`, `_any_type == T` | - |
| `std::any_cast<T>(&a)` | - | Returns non-null iff type matches |
| `a.has_value()` | - | true iff `_has_value == 1` |
| `a.type() == typeid(T)` | - | Proof: `_any_type == T` |
| `a.reset()` | - | `_has_value = 0` |
| `a.emplace<T>(...)` | - | `_has_value = 1`, `_any_type = T` |
| `a = value` | - | `_has_value = 1`, `_any_type` = type of value |

**std::expected** (C++23) — tracks `_has_value` (whether value or error):

```cpp
std::expected<int, std::error_code> e;  // _has_value = 1 (default constructs T)
e.value();                               // OK

std::expected<int, std::error_code> err = std::unexpected(make_error());
err.value();                             // ERROR: _has_value != 1
err.error();                             // OK: error() requires _has_value == 0

if (e.has_value()) {
    e.value();                           // OK: guard proves _has_value == 1
}

if (e) {
    *e;                                  // OK: operator bool proves _has_value == 1
}
```

| Method | Precondition | Postcondition |
|--------|--------------|---------------|
| `value()` | `_has_value == 1` | - |
| `operator*`, `operator->` | `_has_value == 1` | - |
| `error()` | `_has_value == 0` | - |
| `has_value()` | - | true iff `_has_value == 1` |
| `operator bool` | - | true iff `_has_value == 1` |
| `emplace(...)` | - | `_has_value = 1` |

### Smart Pointers

**std::unique_ptr, std::shared_ptr** — tracks `_nonnull`:

```cpp
std::unique_ptr<int> p;      // _nonnull = 0
*p;                          // ERROR: requires _nonnull == 1

p = std::make_unique<int>(42);  // _nonnull = 1
*p = 10;                     // OK
p->something();              // OK

if (p) {
    *p;                      // OK: operator bool proves _nonnull == 1
}

p.reset();                   // _nonnull = 0
p.reset(new int(5));         // _nonnull = 1
```

| Method | Precondition | Postcondition |
|--------|--------------|---------------|
| `operator*`, `operator->` | `_nonnull == 1` | - |
| `operator bool` | - | true iff `_nonnull == 1` |
| `reset()` | - | `_nonnull = 0` |
| `reset(ptr)` | - | `_nonnull = 1` |
| `release()` | - | `_nonnull = 0` |

**std::weak_ptr** — `expired()` returns true iff `_nonnull == 0`.

### Custom Template Types

```cpp
template<typename T, size_t N>
struct FixedBuffer {
    T data[N];

    // Use _tparam1 (second non-type param = N)
    [[clang::annotate("dtt:requires=arg0 < _tparam1")]]
    T& operator[](size_t i) { return data[i]; }
};

FixedBuffer<int, 10> buf;
buf[9];   // OK
buf[10];  // ERROR
```

## C Library Support

DTT tracks C library functions for null pointer safety, use-after-free detection, and **automatic resource leak detection**.

### Automatic Resource Leak Detection

DTT automatically detects resource leaks for C library functions — no manual annotations required.

Resource ghost fields use **`when` guards** for sound semantics: `@open` and `@allocated` are only set when the result is proven non-null. This avoids false positives when allocation fails (returns NULL) but means a null check is needed before DTT tracks the resource:

```cpp
void correct_cleanup() {
    FILE* f = fopen("data.txt", "r");
    if (!f) return;                // proves f non-null → @open set to 1
    // ... use file ...
    fclose(f);                     // OK: sets @open = 0
}

void checked_cleanup() {
    void* p = malloc(100);
    if (p) {                       // proves p non-null → @allocated set to 1
        // ... use memory ...
        free(p);                   // OK: sets @allocated = 0
    }
}
```

**Tracked Resource Pairs:**

| Category | Acquire Functions | Release Functions | Ghost Field |
|----------|-------------------|-------------------|-------------|
| **FILE I/O** | `fopen`, `tmpfile`, `fdopen`, `popen`, `freopen` | `fclose`, `pclose` | `@open` |
| **Memory** | `malloc`, `calloc`, `realloc`, `aligned_alloc`, `strdup`, `strndup` | `free` | `@allocated` |
| **File Descriptors** | `open`, `openat`, `creat`, `dup`, `dup2`, `dup3`, `pipe`, `socket`, `accept`, `accept4` | `close` | `@fd` |
| **Directories** | `opendir`, `fdopendir` | `closedir` | `@dir` |
| **Dynamic Libraries** | `dlopen` | `dlclose` | `@dl` |
| **Memory Mapping** | `mmap` | `munmap` | `@mapped` |
| **Semaphores** | `sem_open` | `sem_close` | `@sem` |
| **Message Queues** | `mq_open` | `mq_close` | `@mq` |
| **Shared Memory** | `shm_open` | (use `close`) | `@shm` |
| **iconv** | `iconv_open` | `iconv_close` | `@iconv` |
| **Regex** | `regcomp` | `regfree` | `@regex` |
| **Address Info** | `getaddrinfo` | `freeaddrinfo` | `@addrinfo` |

**Ownership Transfer:**

DTT understands that returning a resource pointer transfers ownership to the caller:

```cpp
void* allocate(size_t n) {
    void* p = malloc(n);
    return p;  // OK: ownership transferred to caller
}

FILE* open_config() {
    FILE* f = fopen("config.txt", "r");
    return f;  // OK: caller is responsible for fclose
}
```

**Early Return Leak Detection:**

```cpp
void process_file() {
    FILE* f = fopen("data.txt", "r");
    if (!f) return;  // OK: f is NULL, no leak

    if (some_error) {
        return;  // ERROR: f.@open == 1, leak!
    }

    fclose(f);  // OK path
}
```

### FILE* Null Tracking

```cpp
FILE* f = fopen("data.txt", "r");
fgetc(f);        // ERROR: FILE* may be null (fopen can fail)

if (f) {
    fgetc(f);    // OK: guard proves f is non-null
    fclose(f);   // OK, and invalidates f
}

fgetc(f);        // ERROR: FILE* may be null (closed)
```

**Tracked functions requiring non-null FILE\*:**

| Category | Functions |
|----------|-----------|
| Read operations | `fgetc`, `getc`, `fgets`, `fread`, `fscanf` |
| Write operations | `fputc`, `putc`, `fputs`, `fwrite`, `fprintf` |
| Position | `ftell`, `fseek`, `rewind`, `fgetpos`, `fsetpos` |
| Status | `feof`, `ferror`, `clearerr`, `fflush` |
| Close | `fclose` (also invalidates the FILE*) |

### Libc Null Pointer Safety

DTT also tracks null pointer requirements for C library string, conversion, and utility functions:

| Category | Functions |
|----------|-----------|
| String examination | `strlen`, `strcmp`, `strncmp`, `strchr`, `strrchr`, `strstr`, `strpbrk`, `strcspn`, `strspn` |
| String copy/concat | `strcpy`, `strncpy`, `strcat`, `strncat` |
| Memory operations | `memcpy`, `memmove`, `memset`, `memcmp`, `memchr` |
| Conversion (int) | `atoi`, `atol`, `atof`, `strtol`, `strtoll`, `strtoul`, `strtoull` |
| Conversion (float) | `strtof`, `strtod`, `strtold` |
| I/O | `puts`, `fputs`, `printf`, `fprintf` |
| Environment | `getenv` |
| Time | `localtime`, `gmtime`, `ctime`, `asctime`, `mktime`, `strftime` |

All of these require their pointer arguments to be non-null. DTT reports an error when a potentially-null pointer is passed without a null check:

```cpp
char* s = getenv("HOME");  // May return NULL
size_t n = strlen(s);      // ERROR: string pointer may be null

if (s) {
    size_t n = strlen(s);  // OK: guard proves non-null
}
```

### Use-After-Free Detection

DTT checks all pointer arguments at every function call site. If a freed pointer is passed to any function, DTT reports an error:

```cpp
char* buf = (char*)malloc(100);
free(buf);
strlen(buf);         // ERROR: use-after-free of 'buf'
setenv(buf, "v", 1); // ERROR: use-after-free of 'buf'
any_function(buf);   // ERROR: use-after-free of 'buf'
```

This is universal - no per-function annotation required.

## Buffer Pointer Tracking

DTT tracks bounds for raw pointers derived from arrays — a "compile-time span":

```cpp
int arr[10];
int* p = arr;        // DTT knows: _base_length=10, _offset=0, _size=10

p[9] = 1;            // OK: 9 < 10
p[10] = 1;           // ERROR: 10 >= 10
```

### What Gets Tracked

| Source | Tracking |
|--------|----------|
| `int* p = arr` | Array decay → full bounds |
| `int* p = &arr[3]` | Address-of element → adjusted offset |
| `int* p = arr + k` | Pointer arithmetic → offset tracked |
| `int* p = new int[N]` | `new[]` → bounds from size |
| `char* c = (char*)arr` | Type cast → bytes preserved, elements recomputed |

### Pointer Arithmetic Bounds

```cpp
int arr[10];
int* p = arr + 3;    // _offset=3, _size=7

p[6] = 1;            // OK: 6 < 7
p[7] = 1;            // ERROR: 7 >= 7

int* q = arr + 11;   // ERROR: creates pointer beyond one-past-end
int* r = arr - 1;    // ERROR: creates pointer before array start
```

### Dereference Checking

```cpp
int arr[10];
int* p = arr + 10;   // One-past-end is valid for iterator idioms

*p = 1;              // ERROR: dereferencing one-past-end
```

### Type Cast Bounds Preservation

All cast syntaxes preserve buffer bounds — `reinterpret_cast`, C-style casts, and `static_cast` through `void*`:

```cpp
int arr[10];                              // 40 bytes

// All equivalent - DTT tracks through each
char* c1 = reinterpret_cast<char*>(arr);  // _size = 40 chars
char* c2 = (char*)arr;                    // _size = 40 chars
void* v = static_cast<void*>(arr);
char* c3 = static_cast<char*>(v);         // _size = 40 chars

c1[39] = 1;          // OK: 39 < 40
c1[40] = 1;          // ERROR: 40 >= 40
```

Address-of patterns also preserve bounds:

```cpp
unsigned int data[3];                      // 12 bytes
unsigned char* p = (unsigned char*)&data[0];  // _size = 12 bytes

p[11] = 0xFF;        // OK: 11 < 12
p[12] = 0xFF;        // ERROR: 12 >= 12

// Struct address
struct Header { int a, b, c; };            // 12 bytes
Header h;
char* bytes = reinterpret_cast<char*>(&h); // _size = 12 bytes
```

### Type Range Bounds Inference

When an index variable's type guarantees it cannot exceed the array size, DTT proves safety without Z3:

```cpp
int table[256];
unsigned char idx = get_byte();  // 0-255 by type

table[idx] = 42;     // OK: unsigned char max (255) < 256

unsigned short lookup[65536];
unsigned short h = compute_hash();

lookup[h] = 0;       // OK: unsigned short max (65535) < 65536
```

This eliminates false positives for common lookup table patterns.

### Null Pointer Interaction

```cpp
int arr[10];
int* p = arr;        // buffer(_size=10)
p = nullptr;         // now null (buffer tracking cleared)
p = arr;             // buffer(_size=10) restored
```

### Null Pointer Arithmetic (UB Detection)

Pointer arithmetic on a null pointer is undefined behavior. DTT catches this at the arithmetic site:

```cpp
int* p = nullptr;
int* q = p + 5;      // ERROR: pointer arithmetic on null pointer is UB
++p;                 // ERROR: pointer arithmetic on null pointer is UB
p += 1;              // ERROR: pointer arithmetic on null pointer is UB
```

All forms are detected: `p + k`, `p - k`, `++p`, `p++`, `--p`, `p--`, `p += k`, `p -= k`.

### Use-After-Free Detection

DTT tracks pointer lifetime and detects use-after-free bugs. When memory is freed via `free()`, `delete`, or `delete[]`, the pointer is marked as invalid:

```cpp
int* p = new int(42);
delete p;
*p = 10;              // ERROR: use-after-free of 'p'

char* buf = (char*)malloc(100);
free(buf);
buf[0] = 'x';         // ERROR: use-after-free of 'buf'
```

**Function Arguments**

DTT checks all pointer arguments at every function call site. If a freed pointer is passed to any function, DTT reports an error:

```cpp
char* str = (char*)malloc(100);
free(str);
strlen(str);          // ERROR: use-after-free of 'str'
setenv(str, "v", 1);  // ERROR: use-after-free of 'str'
any_function(str);    // ERROR: use-after-free of 'str'
```

This is universal - no per-function annotation required.

**Double-Free Detection**

DTT also catches double-free bugs:

```cpp
int* p = new int(42);
delete p;
delete p;             // ERROR: double-free of 'p'
```

**Mismatched Deallocation**

DTT detects when allocation and deallocation methods don't match:

```cpp
int* p = new int[10];
delete p;             // ERROR: mismatched deallocation (new[] vs delete)

int* q = (int*)malloc(40);
delete q;             // ERROR: mismatched deallocation (malloc vs delete)
```

## Uninitialized Memory Detection

DTT detects use of uninitialized variables (CWE-457) when passed to functions.

### Uninitialized Scalars and Pointers

```cpp
void sink_int(int v);
void sink_ptr(int* p);

void example() {
    int x;           // Uninitialized
    sink_int(x);     // ERROR: reading uninitialized variable 'x'

    int* p;          // Uninitialized pointer
    sink_ptr(p);     // ERROR: reading uninitialized pointer 'p'

    int y = 42;
    sink_int(y);     // OK: y is initialized

    int* q = nullptr;
    sink_ptr(q);     // OK: q is initialized (to null)
}
```

### Uninitialized Struct Members (POD Types)

DTT tracks individual members of POD (Plain Old Data) structs:

```cpp
struct Point { int x; int y; };

void sink_int(int v);
void sink_point(Point p);

void example() {
    Point p;         // Members indeterminate (POD with no initializer)
    sink_int(p.x);   // ERROR: p.x is uninitialized

    Point q;
    q.x = 1;
    sink_int(q.x);   // OK: q.x is initialized
    sink_int(q.y);   // ERROR: q.y still uninitialized

    Point r;
    r.x = 1;
    r.y = 2;
    sink_point(r);   // OK: all members initialized
}
```

### Partial Struct Initialization

When passing a struct by value, all members are read (copied). DTT catches partially initialized structs:

```cpp
void sink_point(Point p);

void example() {
    Point p;
    p.x = 1;         // Only x initialized
    sink_point(p);   // ERROR: copying reads p.y (uninitialized)
}
```

### Nested Struct Members

DTT recursively tracks nested struct members:

```cpp
struct Line { Point start; Point end; };

void example() {
    Line l;          // All nested members indeterminate
    sink_int(l.start.x);  // ERROR: l.start.x uninitialized

    Line m;
    m.start.x = 0;
    m.start.y = 0;
    m.end.x = 10;
    m.end.y = 10;
    // Now all members initialized
}
```

### Safe Patterns (No Error)

```cpp
// Brace initialization (zero-initialized)
Point p{};
sink_point(p);       // OK

// Designated initializers
Point q{.x = 1, .y = 2};
sink_point(q);       // OK

// Aggregate initialization
Point r = {1, 2};
sink_point(r);       // OK

// Out-parameter pattern (address taken)
void fill_point(Point* out);
Point s;
fill_point(&s);      // OK: taking address, not reading
sink_point(s);       // OK: assumed filled by fill_point
```

### Known Limitation: Array Elements

Struct members within arrays are not yet tracked per-element:

```cpp
Point pts[2];        // All members indeterminate
sink_int(pts[0].x);  // GAP: should error but doesn't yet
```

### C Varargs, Variadic Templates, and Fold Expressions

DTT tracks uninitialized values through all forms of C and C++ ellipsis patterns:
C-style varargs (`...`), variadic template parameter packs (`typename... Args`),
pack expansions, and C++17 fold expressions.

#### C Varargs

```cpp
int printf(const char*, ...);

void example() {
    int x;
    printf("%d", x);        // ERROR: x is uninitialized

    int y = 42;
    printf("%d", y);        // OK: y is initialized
}
```

#### Variadic Template Parameter Packs

```cpp
template<typename... Args>
void consume(Args... args);

void example() {
    int a = 1, b, c = 3;
    consume(a, b, c);       // ERROR: b is uninitialized

    consume(1, 2.0, '3');   // OK: literals always initialized
}
```

DTT tracks through all pack-related patterns:
- Basic variadic calls and recursive variadic templates
- `sizeof...(args)` (compile-time, never evaluates)
- Pack expansion in function calls, initializer lists, and arrays
- `if constexpr` dispatch with pack peeling
- Variadic `using` declarations (overloaded visitor pattern)
- `std::apply` and `std::invoke` tuple unpacking
- Pack expansion in lambda init-captures (C++20)
- CTAD with variadic deduction guides
- Perfect forwarding (`std::forward<Args>(args)...`)

#### Fold Expressions (C++17)

DTT catches uninitialized values in all four fold forms:

```cpp
template<typename... Args>
auto sum(Args... args) { return (args + ...); }  // Unary right fold

void example() {
    int a = 1, b, c = 3;
    auto r = sum(a, b, c);  // ERROR: b is uninitialized
}
```

Supported fold operators: `+`, `-`, `*`, `/`, `|`, `^`, `&`, `&&`, `||`, `,`,
and compound assignment folds (`+=`, `|=`, `*=`, `<<=`, etc.).

#### Known Limitations

**Fold push_back on local vectors works:** After `(v.push_back(args), ...)` with N pack
elements on a local vector, DTT correctly counts the N push_backs by analyzing the
template instantiation where the fold is fully expanded.

**Reference parameter limitation:** When fold push_back operates on a vector passed by
reference (declared in the caller), DTT does not propagate the `_size` changes back to
the caller. This is a general inter-procedural limitation, not specific to fold expressions.

**Pack parameter property tracking:** Container `_size` properties are not fully propagated
through variadic template pack parameters. DTT now warns (Category D: solver-failed) about
unverified bounds on pack parameter accesses, but cannot prove them safe or unsafe without
inter-procedural property tracking.

## Automatic Annotations

DTT automatically adds constraints for certain well-known patterns.

### main() argc Parameter

Per the C++ standard, `argc` is always >= 1 (argv[0] contains the program name). DTT automatically adds this constraint for `main`'s first integer parameter, regardless of the parameter name:

```cpp
int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    args.resize(argc);      // _size = argc

    args[0] = argv[0];      // OK: argc >= 1 implies _size >= 1
    args[1] = "default";    // WARNING: argc > 1 not proven
}
```

This enables safe access to `argv[0]` and similar patterns without explicit guards.

## Value Concepts

Value Concepts extend DTT with bounded quantifiers for verifying collection-wide properties.

### Bounded Quantifiers

```cpp
// Universal quantifier: all elements satisfy P
[[clang::annotate("dtt:requires=forall(i, 0, n, arr[i] > 0)")]]
double geometric_mean(const int* arr, size_t n);

// Existential quantifier: some element satisfies P
[[clang::annotate("dtt:requires=exists(i, 0, n, arr[i] == target)")]]
int find_index(const int* arr, size_t n, int target);
```

### Predefined Concepts

| Concept | Meaning |
|---------|---------|
| `nonempty` | `_size > 0` |
| `empty` | `_size == 0` |
| `sorted` | Adjacent elements in order |
| `nonnull` | `_nonnull == 1` |

```cpp
[[clang::annotate("dtt:requires=nonempty")]]
int front(const std::vector<int>& v);

[[clang::annotate("dtt:requires=sorted")]]
int binary_search(const std::vector<int>& v, int target);
```

### User-Defined Concepts

Define custom concepts by combining existing ones:

```cpp
[[clang::annotate("dtt:define=searchable := nonempty && sorted")]]
[[clang::annotate("dtt:requires=searchable")]]
int safe_binary_search(const std::vector<int>& v, int target);
```

### Auto-Inference

DTT automatically establishes concepts from STL operations:

| Operation | Established Concept |
|-----------|---------------------|
| `std::sort(begin, end)` | `sorted` |
| Array literal `{1,2,3}` | Element values tracked for `forall` |

```cpp
std::vector<int> v = {3, 1, 4};
binary_search(v, 3);           // ERROR: not sorted

std::sort(v.begin(), v.end()); // Establishes 'sorted'
binary_search(v, 3);           // OK
```

### Auto-Postcondition Inference

DTT can automatically infer postconditions from method implementations without requiring explicit `ensures=` annotations:

```cpp
struct Vec {
    int _size;

    // DTT infers: ensures=_size == n
    void resize(int n) {
        _size = n;
    }

    // DTT infers: ensures=_size == old(_size) + 1
    void push() {
        _size++;
    }

    // DTT infers: ensures=_size == a * b + c
    void set_computed(int a, int b, int c) {
        _size = a * b + c;
    }
};

void example() {
    Vec v;
    v._size = 0;
    v.resize(10);    // DTT knows: _size == 10
    v.front();       // OK: 10 > 0
}
```

**Supported patterns:**
| Pattern | Inferred Postcondition |
|---------|------------------------|
| `_size = n` | `ensures=_size == arg0` |
| `_size = n + 1` | `ensures=_size == arg0 + 1` |
| `_size++` | `ensures=_size == old(_size) + 1` |
| `_size = _size * 2` | `ensures=_size == old(_size) * 2` |
| `stats.count = n` | `ensures=stats.count == arg0` |

**Nested fields:** Nested member paths like `stats.count` are tracked when assigned within methods.

**Limitations:**
- Array-indexed fields (`bufs[0]._size`) not yet supported
- Multi-statement local variable tracking limited
- Equality comparisons in ternary conditions not resolved

### Limitations

- **Bounded only**: `forall(i, 0, n, P)` not `forall(i, P)`
- **No loop invariants**: Cannot infer from loop bodies (future work)
- **No parameterized concepts**: `all_in_range(0, 256)` not yet supported

See [VALUE_CONCEPTS_SPEC.md](docs/VALUE_CONCEPTS_SPEC.md) for full specification.

## Advanced Analysis Features

### Transitive Precondition Propagation

When a function calls another function that has inferred preconditions, those preconditions propagate upward through the call chain:

```cpp
void inner(char* buf, size_t n) {
    memcpy(buf, "data", n);  // Infers: buf._size_bytes >= n
}

void middle(char* buf, size_t n) {
    inner(buf, n);  // Inherits: buf._size_bytes >= n
}

void outer(char* buf, size_t n) {
    middle(buf, n);  // Also inherits: buf._size_bytes >= n
}

void caller() {
    char small[4];
    outer(small, 100);  // WARNING: 4 < 100
}
```

DTT uses fixpoint iteration to compute the transitive closure of preconditions across call chains within the same translation unit.

#### Expression Propagation

Preconditions propagate through complex expressions, not just direct parameter passing:

**Pointer Arithmetic:**
```cpp
void copy_inner(char* dst, size_t n) {
    memcpy(dst, "data", n);  // Infers: dst._size_bytes >= n
}

void copy_with_offset(char* buf, size_t n, size_t offset) {
    copy_inner(buf + offset, n);  // Propagates: buf._size_bytes >= n + offset
}

void caller() {
    char small[10];
    copy_with_offset(small, 8, 5);  // WARNING: 10 < 8 + 5 = 13
}
```

**Size Expressions:**
```cpp
void copy_inner(char* buf, size_t n) {
    memcpy(buf, "data", n);
}

void copy_half(char* buf, size_t total) {
    copy_inner(buf, total / 2);  // Propagates: buf._size_bytes >= total / 2
}

void copy_elements(int* buf, size_t count) {
    memcpy(buf, src, count * sizeof(int));  // Propagates: buf._size_bytes >= count * 4
}

void caller() {
    char small[10];
    copy_half(small, 30);  // WARNING: 10 < 30 / 2 = 15
}
```

#### Member Path Resolution

DTT resolves struct member paths to determine actual buffer sizes:

```cpp
struct MyStruct {
    char buf[10];
    int other;
};

void copy_inner(char* dst, size_t n) {
    memcpy(dst, "data", n);
}

void copy_member(MyStruct* s, size_t n) {
    copy_inner(s->buf, n);  // Propagates: s->buf._size_bytes >= n
}

void caller() {
    MyStruct s;
    copy_member(&s, 15);  // WARNING: s.buf is 10 bytes, need 15
    copy_member(&s, 8);   // OK: s.buf is 10 bytes, need 8
}
```

#### Nested Member Navigation

DTT navigates through nested struct hierarchies to find array sizes:

```cpp
struct Inner {
    char buf[15];
};

struct Outer {
    Inner inner;
    int flags;
};

struct Level3 {
    Level2 l2;  // Level2 contains Level1 which has char data[10]
};

void copy_inner(char* dst, size_t n) {
    memcpy(dst, "data", n);
}

void copy_nested(Outer* o, size_t n) {
    copy_inner(o->inner.buf, n);  // Resolves o->inner.buf size = 15
}

void copy_deep(Level3* l3, size_t n) {
    copy_inner(l3->l2.l1.data, n);  // Navigates 3 levels deep
}

void caller() {
    Outer o;
    copy_nested(&o, 20);  // WARNING: o.inner.buf is 15 bytes
    copy_nested(&o, 10);  // OK
}
```

#### Combined Expressions

Member access combines with pointer arithmetic:

```cpp
void copy_member_offset(MyStruct* s, size_t n, size_t off) {
    copy_inner(s->buf + off, n);  // Propagates: s->buf._size_bytes >= n + off
}

void caller() {
    MyStruct s;  // s.buf is 10 bytes
    copy_member_offset(&s, 5, 8);  // WARNING: 10 < 5 + 8 = 13
    copy_member_offset(&s, 3, 5);  // OK: 10 >= 3 + 5 = 8
}
```

#### Z3 SMT Verification

At call sites, DTT uses the Z3 SMT solver to verify symbolic bounds. This handles complex arithmetic and proves whether concrete buffer sizes satisfy propagated preconditions:

```cpp
void wrapper(char* buf, size_t n, size_t m) {
    copy_inner(buf, n + m);  // Propagates: buf._size_bytes >= n + m
}

void caller() {
    char buf[20];
    wrapper(buf, 8, 15);   // Z3 checks: 20 >= 8 + 15 → WARNING: 20 < 23
    wrapper(buf, 8, 10);   // Z3 checks: 20 >= 8 + 10 → OK: 20 >= 18
}
```

Z3 evaluates the symbolic bound expression with concrete argument values to determine if a warning should be emitted.

### Frame Inference (Effect Tracking)

DTT tracks which fields each function writes. Fields that are NOT written are "framed" — proofs about them survive across the function call:

```cpp
void modify_size(std::vector<int>& v) {
    v.push_back(1);  // Writes: _size
}

void read_only(const std::vector<int>& v) {
    // Writes: nothing (pure with respect to v)
}

void example() {
    std::vector<int> v;
    v.resize(10);         // v._size = 10

    read_only(v);         // Frame: _size not written
    v[5] = 42;            // OK: proof v._size >= 6 survives

    modify_size(v);       // Effect: _size written
    v[5] = 42;            // WARNING: _size proof invalidated
}
```

### Counterexamples (Refutations)

When DTT cannot verify a constraint, it now provides concrete counterexample values to help diagnose the issue:

```cpp
void process(char* buf, size_t n) {
    memcpy(buf, "data", n);
}

void caller() {
    char small[4];
    process(small, 100);
    // error: DTT: memcpy destination buffer may be too small
    // note: counterexample: buf._size_bytes = 4, required = 100 (4 >= 100 is false)
}
```

The counterexample shows the actual tracked values and why the constraint fails, making it easier to understand and fix the issue.

## Known Limitations

DTT has some analysis gaps where certain code patterns are not fully supported. These are tracked here for transparency.

### Range-for loops don't establish proofs

Range-for provides no bounds proofs for index usage. Mixed range-for/index patterns may produce false positives.

```cpp
for (auto& item : vec) {
    // No index proof available here
}
```

### Ternary guard not fully propagated

`Data* chosen = pick ? ptr : fallback;` doesn't infer non-null when both operands are guarded variables. Works for literal non-null (address-of, new, string literals).

### Member-call boolean guards without annotations

`if (h.hasValue())` provides no proof unless the method has `ensures_if_true` annotations.

```cpp
struct Handle {
    [[clang::annotate("dtt:ensures_if_true=_valid == 1")]]
    bool hasValue() const;  // Need annotation for proof
};
```

### Mutually exclusive enum/integer branches not tracked

`if (cmdId == X) { ptr = null; } if (cmdId == Y) { *ptr; }` warns even though X≠Y. Switch-like if-else chains may generate false positives.

### Intentional crash patterns not suppressible

`void CrashMe() { char* p = nullptr; *p = 0; }` warns about null dereference. Debug/assert code that intentionally crashes has no suppression mechanism.

### If-else branch assignment not tracked

Variables assigned in all branches of if-else may still report "uninitialized":

```cpp
T* p;
if (c) p = f1();
else p = f2();
// DTT may not know p is initialized
```

### Fold expression push_back on reference parameters

Fold push_back/emplace_back counting works on **local** vectors (DTT analyzes
template instantiations where folds are expanded). However, when the vector
is passed by **reference** from the caller, the `_size` changes are not
propagated back:

```cpp
template<typename... Args>
void add_all(std::vector<int>& v, Args... args) {
    (v.push_back(args), ...);  // N push_backs counted inside
}
std::vector<int> v;
add_all(v, 1, 2, 3);
v[0];  // FALSE POSITIVE: caller doesn't see _size = 3
```

This is a general inter-procedural limitation, not specific to fold expressions.

### Pack parameter property propagation

Container properties are not propagated through variadic template pack parameters:

```cpp
template<typename... Vecs>
void check(Vecs&... vecs) {
    ((vecs[0]), ...);  // FALSE NEGATIVE: OOB not caught
}
std::vector<int> a, b;  // Both _size = 0
check(a, b);             // DTT silent on the OOB access
```

## Version History

| Version | Features |
|---------|----------|
| **v36** | Conditional annotations (`when` guards in `.dtt`); Provenance chains (full state history on error); FixItHint suggestions for common preconditions; Backward search over annotation registry for computed fix suggestions on any type; Generalized Datalog-style fixpoint for transitive propagation of all property constraints (null, resource, size); Libc null-pointer annotations for 30+ string/conversion/time functions |
| **v35** | Uninitialized Memory Detection (CWE-457): Detects uninitialized scalars, pointers, and POD struct members passed to functions; tracks partial struct initialization; recursive nested struct member tracking |
| **v34** | Automatic C Library Resource Leak Detection: `fopen`/`fclose`, `malloc`/`free`, `open`/`close`, and 40+ other resource pairs tracked automatically via ghost fields (`@open`, `@allocated`, `@fd`, etc.); ownership transfer on return recognized; early return leak detection |
| **v33** | Auto-Postcondition Inference: Methods with direct field assignments (`_size = n`) automatically infer postconditions without annotations; supports arithmetic expressions, nested fields (`stats.count`), and parameter references |
| **v32** | Advanced Analysis: Transitive precondition propagation through call chains; Frame inference tracking field writes per function; Counterexample generation for verification failures |
| **v31** | Guard-based Buffer Bounds: `if (idx < N) { arr[idx]; }` proves safe access via Z3; supports all comparison forms (`i < 8`, `8 > i`, `!(i >= 8)`); correct unsigned type handling |
| v30 | Cast & Type Range Improvements: `&arr[0]` and `&struct` track full size through all cast types (reinterpret_cast, C-style, static_cast via void*); Type range bounds inference for `unsigned char` (→255) and `unsigned short` (→65535) indices |
| v29 | Early Exit Proofs: `if (cond) { return/throw/break/continue; }` proves `!cond` afterward; Ghost State: `@prefix` for unforgeable compile-time-only properties; Move Construction Tracking: `Vec v2(std::move(v1))` copies properties and invalidates source |
| v28 | Buffer Pointer Tracking: bounds for raw pointers from arrays, `new[]`, casts; OOB detection on `p[n]`, `*p`, pointer arithmetic |
| v27 | Informative Additive Safety: `--dtt-mode=inform\|enforce\|quiet`, Category C/D warnings, annotation conflict detection (Category G) |
| v26 | Loop: `!=` condition, pre-declared var (`int i; for(i=0;...)`), increment direction verification |
| v25 | Loop bounds: non-zero start (`i=1`), constant bounds (`i<10`), `<=`, any increment (`i++`/`++i`/`i+=1`) |
| v24 | VarBoundProofs invalidation on field mutation (soundness fix) |
| v23 | Overflow-safe arithmetic in SymValue (prevents UB) |
| v22 | Z3 C++ API integration (replaces shell-out), eliminates temp files |
| v21 | Soundness: rvalue ref (T&&), non-const pointer, non-void function invalidation, std::move tracing |
| v20 | Conservative reference invalidation, lambda isolation, function scope |
| v19 | Mul/div constraints, equality/inequality, nested guards |
| v18 | std::array, std::span, std::bitset bounds |
| v17 | Template parameter extraction |
| v16 | Assignment & swap tracking, move invalidation |
| v15 | Index arithmetic, return size tracking |
| v14 | Arithmetic expressions, transitive proofs |
| v13 | Loop bounds, runtime guards |
| v12 | Symbolic argument tracking |
| v11 | Index bounds checking |
| v10 | Method tracking, std::vector |
| v9 | old(), Boolean returns |
| v8 | Constant array indices |
| v7 | Multi-arg, Switch totality |
| v6 | Z3 SMT solver |
| v5 | Struct field paths |
| v4 | Proof persistence |
| v3 | Postconditions, else-guards |
| v2 | Any property, AND/OR |
| v1 | Basic tracking |
