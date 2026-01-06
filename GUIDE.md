# DTT User Guide

Detailed documentation for the DTT (Dependent Type Tracking) Clang plugin.

## Table of Contents

- [Features](#features)
- [Annotation Reference](#annotation-reference)
- [Method Call Tracking](#method-call-tracking)
- [dtt::vector\<T\>](#dttvectort)
- [Index Bounds Checking](#index-bounds-checking)
- [Symbolic Argument Tracking](#symbolic-argument-tracking)
- [Arithmetic Expressions](#arithmetic-expressions)
- [Transitive Proofs](#transitive-proofs)
- [Assignment & Swap Tracking](#assignment--swap-tracking)
- [Standard Library Support](#standard-library-support)
- [Version History](#version-history)

## Features

| Feature | Status |
|---------|--------|
| Track any field | ✓ |
| Compound constraints (AND/OR) | ✓ |
| Symbolic arithmetic | ✓ |
| Runtime guards as proofs | ✓ |
| Postconditions | ✓ |
| Else-branch guards | ✓ |
| Proof persistence/copying | ✓ |
| Struct field paths | ✓ |
| Z3 SMT solver | ✓ |
| Multi-arg constraints | ✓ |
| Switch totality | ✓ |
| Constant array indices | ✓ |
| old() references | ✓ |
| Boolean return postconditions | ✓ |
| Method call tracking | ✓ |
| dtt::vector<T> | ✓ |
| Index bounds checking | ✓ |
| Symbolic argument tracking | ✓ |
| Loop bounds tracking | ✓ |
| Arithmetic expressions | ✓ |
| Transitive proofs | ✓ |
| Index arithmetic | ✓ |
| Return size tracking | ✓ |
| Copy/move assignment | ✓ |
| swap() tracking | ✓ |
| Move invalidation | ✓ |
| Template parameter extraction | ✓ |
| std::array bounds | ✓ |
| std::span bounds | ✓ |
| std::bitset bounds | ✓ |

## Annotation Reference

### Preconditions

```cpp
// Require condition before call
[[clang::annotate("dtt:requires=_size > 0")]]
void pop();

// Multiple arguments
[[clang::annotate("dtt:requires=arg0 < _size")]]
T& operator[](size_t i);

// Arithmetic expressions
[[clang::annotate("dtt:requires=arg0 + arg1 <= _size")]]
void copy(int start, int len);
```

### Postconditions

```cpp
// Set property after call
[[clang::annotate("dtt:ensures=_size == 0")]]
void clear();

// Reference old value
[[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
void push();

// Track argument value
[[clang::annotate("dtt:ensures=_size == arg0")]]
void resize(size_t n);
```

### Return Value Tracking

```cpp
// Return tracked property
[[clang::annotate("dtt:returns=_size")]]
size_t size() const;

// Boolean branch proofs
[[clang::annotate("dtt:ensures_if_true=_size == 0")]]
[[clang::annotate("dtt:ensures_if_false=_size > 0")]]
bool empty() const;
```

## Method Call Tracking

Annotate class methods with contracts:

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

Container c;  // _size = 0 (from constructor postcondition)
c.push();     // _size = 1
c.push();     // _size = 2

if (!c.empty()) {
    c.pop();  // ✓ OK: !empty() proves _size > 0
}

c.pop();
c.pop();
c.pop();      // ✗ ERROR: _size = 0, requires > 0
```

## dtt::vector\<T\>

A tracked wrapper around `std::vector<T>`:

```cpp
#include "dtt_vector.h"

dtt::vector<int> v;

v.push_back(1);  // _size = 1
v.push_back(2);  // _size = 2

if (!v.empty()) {
    v.front();    // ✓ OK
    v.pop_back(); // ✓ OK
}

v.clear();
v.pop_back();    // ✗ ERROR: _size = 0
```

**Tracked methods:**
- `size()` — returns tracked `_size`
- `empty()` — returns true iff `_size == 0`
- `push_back()` — increments `_size`
- `pop_back()` — requires `_size > 0`, decrements
- `front()`, `back()` — require `_size > 0`
- `clear()` — sets `_size = 0`
- `resize(n)` — sets `_size = n`
- `assign(n, x)` — sets `_size = n`

## Index Bounds Checking

Compile-time detection of out-of-bounds array access:

```cpp
dtt::vector<int> v;
v.push_back(1);  // _size = 1
v.push_back(2);  // _size = 2

v[0] = 10;  // ✓ OK: 0 < 2
v[1] = 20;  // ✓ OK: 1 < 2
v[2] = 30;  // ✗ ERROR: 2 < 2 is false
```

**Error message:**
```
error: DTT: vector::operator[] index 2 out of bounds (requires < 2, but v._size = 2)
```

**Notes:**
- Only constant indices are checked at compile time
- Runtime indices with guards are checked via proof system
- Works with any tracked property, not just `_size`

## Symbolic Argument Tracking

Method arguments can be tracked symbolically in postconditions:

```cpp
[[clang::annotate("dtt:ensures=_size == arg0")]]
void resize(size_type count);

[[clang::annotate("dtt:ensures=_size == old(_size) + arg1")]]
iterator insert(const_iterator pos, size_type count, const T& value);
```

**Usage:**
```cpp
dtt::vector<int> v;
v.resize(5);               // _size = 5 (constant)
v[4] = 10;                 // ✓ OK: 4 < 5

v.push_back(1);            // _size = 1
v.insert(v.begin(), 2, 0); // _size = 1 + 2 = 3
v[2] = 20;                 // ✓ OK: 2 < 3

v.assign(10, 42);          // _size = 10
v.front();                 // ✓ OK: _size > 0
```

## Arithmetic Expressions

Constraints can use arbitrary arithmetic expressions with `+`, `-`, `*`:

```cpp
struct Buffer {
    [[clang::annotate("dtt:ensures=_size == arg0")]]
    Buffer(int n);
    
    [[clang::annotate("dtt:requires=arg0 + arg1 <= _size")]]
    void copyRange(int start, int len);
    
    [[clang::annotate("dtt:requires=arg0 * arg1 < _capacity")]]
    void allocateMatrix(int rows, int cols);
};

Buffer buf(10);        // _size = 10
buf.copyRange(0, 5);   // ✓ 0 + 5 = 5 <= 10
buf.copyRange(3, 7);   // ✓ 3 + 7 = 10 <= 10
buf.copyRange(5, 6);   // ✗ ERROR: 5 + 6 = 11 > 10
```

**Supported:** `+`, `-`, `*` with `argN`, `_field`, `old(_field)`, constants

## Transitive Proofs

When a field equals a variable, proofs on that variable transfer:

```cpp
void process(size_t n) {
    dtt::vector<int> v;
    v.resize(n);       // _size = n (symbolic)
    
    if (n > 0) {
        // Proof: n > 0
        // Transitive: _size == n, so _size > 0
        v.front();     // ✓ OK via transitive proof
        v.pop_back();  // ✓ OK via transitive proof
    }
}
```

## Assignment & Swap Tracking

Properties are automatically tracked through assignment and swap:

```cpp
dtt::vector<int> v1, v2;
v1.resize(5);   // v1._size = 5
v2.resize(10);  // v2._size = 10

// Copy assignment
v2 = v1;        // v2._size = 5 (copied from v1)
v2[4];          // ✓ OK: 4 < 5

// Move assignment
v2 = std::move(v1);  // v2._size = 5, v1._size = Unknown
v2[4];               // ✓ OK: 4 < 5
v1.front();          // ✗ ERROR: v1._size is unknown (moved-from)

// Swap
v1.swap(v2);         // Properties exchanged
std::swap(v1, v2);   // Also works
```

**Tracked operations:**
- `v2 = v1` — copy assignment
- `v2 = std::move(v1)` — move assignment (invalidates v1)
- `v1.swap(v2)` — member swap
- `std::swap(v1, v2)` — non-member swap
- `v = v` — self-assignment (no-op)

## Standard Library Support

DTT extracts template parameters and applies bounds checking:

```cpp
// std::array<T, N> — _size = N
std::array<int, 10> arr;
arr[9];   // ✓ OK
arr[10];  // ✗ ERROR

// std::bitset<N> — _size = N
std::bitset<8> bits;
bits[7];  // ✓ OK
bits[8];  // ✗ ERROR

// std::span<T, N> — _size = N (static extent only)
std::span<int, 5> sp(data);
sp[4];    // ✓ OK
sp[5];    // ✗ ERROR
```

## Version History

| Version | Features | Tests |
|---------|----------|-------|
| v1 | Basic tracking | 4 |
| v2 | Any property, AND/OR | 7 |
| v3 | Postconditions, else-guards | 11 |
| v4 | Proof persistence | 14 |
| v5 | Struct field paths | 17 |
| v6 | Z3 SMT solver | 22 |
| v7 | Multi-arg, Switch totality | 27 |
| v8 | Constant array indices | 32 |
| v9 | old(), Boolean returns | 38 |
| v10 | Method tracking, dtt::vector | 46 |
| v11 | Index bounds checking | 48 |
| v12 | Symbolic argument tracking | 51 |
| v13 | Loop bounds, runtime guards | 53 |
| v14 | Arithmetic expressions, transitive proofs | 56 |
| v15 | Index arithmetic, return size tracking | 65 |
| v16 | Assignment & swap tracking, move invalidation | 74 |
| v17 | Template parameter extraction | 76 |
| v18 | std::array, std::span, std::bitset bounds | 88 |
