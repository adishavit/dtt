# DTT User Guide

Detailed documentation for the DTT (Dependent Type Tracking) Clang plugin.

## Table of Contents

- [Features](#features)
- [Annotation Reference](#annotation-reference)
- [Constraint Operators](#constraint-operators)
- [Method Call Tracking](#method-call-tracking)
- [dtt::vector\<T\>](#dttvectort)
- [Index Bounds Checking](#index-bounds-checking)
- [Proof System](#proof-system)
- [Assignment & Swap Tracking](#assignment--swap-tracking)
- [Reference Invalidation](#reference-invalidation)
- [Standard Library Support](#standard-library-support)
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
| **Loop Bounds** | For-loop variable bounds tracking |
| **Copy/Move Assignment** | Properties transfer on `v2 = v1` |
| **Chain Assignment** | `v3 = v2 = v1` properly tracked |
| **Move Invalidation** | `std::move(v)` invalidates source |
| **Swap Tracking** | `swap()` and `std::swap()` exchange properties |
| **Self-Assignment** | `v = v` safely ignored |
| **Template Parameters** | `_tparam0`, `_tparam1` extraction |
| **Z3 SMT Solver** | Complex symbolic constraint solving |
| **dtt::vector\<T\>** | Drop-in tracked vector wrapper |
| **std::array\<T,N\>** | Automatic `_size = N` tracking |
| **std::span\<T,N\>** | Static extent bounds checking |
| **std::bitset\<N\>** | Bit index bounds checking |
| **Reference Invalidation** | Unknown functions invalidate ref args |
| **Lambda Isolation** | Lambda bodies don't affect outer scope |
| **Function Scope** | Variables isolated per function |

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

## dtt::vector\<T\>

Drop-in replacement for `std::vector<T>` with full tracking:

```cpp
#include "dtt_vector.h"

dtt::vector<int> v;
v.push_back(1);     // _size = 1
v.resize(10);       // _size = 10
v[9] = 42;          // OK: 9 < 10

if (!v.empty()) {
    v.pop_back();   // OK: !empty() proves _size > 0
}

v.clear();
v.front();          // ERROR: _size = 0
```

**Tracked methods:**
| Method | Precondition | Postcondition |
|--------|--------------|---------------|
| `size()` | - | returns `_size` |
| `empty()` | - | true iff `_size == 0` |
| `push_back()` | - | `_size = old(_size) + 1` |
| `pop_back()` | `_size > 0` | `_size = old(_size) - 1` |
| `front()`, `back()` | `_size > 0` | - |
| `clear()` | - | `_size = 0` |
| `resize(n)` | - | `_size = n` |
| `assign(n, x)` | - | `_size = n` |
| `operator[](i)` | `i < _size` | - |

## Index Bounds Checking

```cpp
dtt::vector<int> v;
v.push_back(1);
v.push_back(2);  // _size = 2

v[0] = 10;  // OK: 0 < 2
v[1] = 20;  // OK: 1 < 2
v[2] = 30;  // ERROR: 2 < 2 is false
```

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
    dtt::vector<int> v;
    v.resize(n);  // _size = n (symbolic)

    if (n > 0) {
        v.front();  // OK: guard proves n > 0, _size == n
    }
}
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

## Assignment & Swap Tracking

```cpp
dtt::vector<int> v1, v2;
v1.resize(5);   // v1._size = 5
v2.resize(10);  // v2._size = 10

// Copy assignment
v2 = v1;        // v2._size = 5

// Move assignment
v2 = std::move(v1);  // v2._size = 5, v1 invalidated
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

**Custom template types:**
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

## Version History

| Version | Features |
|---------|----------|
| **v24** | VarBoundProofs invalidation on field mutation (soundness fix) |
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
| v10 | Method tracking, dtt::vector |
| v9 | old(), Boolean returns |
| v8 | Constant array indices |
| v7 | Multi-arg, Switch totality |
| v6 | Z3 SMT solver |
| v5 | Struct field paths |
| v4 | Proof persistence |
| v3 | Postconditions, else-guards |
| v2 | Any property, AND/OR |
| v1 | Basic tracking |
