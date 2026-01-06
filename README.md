# DTT: Dependent Type Checking for C++ (v16)

A Clang plugin providing Idris-style dependent type checking with Z3 SMT, method call tracking, index bounds checking, symbolic argument tracking, arithmetic expressions, transitive proofs, **assignment tracking**, and a tracked `dtt::vector<T>` drop-in replacement.

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
| **Copy/move assignment** | ✓ NEW |
| **swap() tracking** | ✓ NEW |
| **Move invalidation** | ✓ NEW |

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

## dtt::vector<T>

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

## Requirements

```bash
apt install clang-18 libclang-18-dev llvm-18-dev z3
```

## Build & Test

```bash
chmod +x build.sh
./build.sh
```

## Test Results (74/74)

```
--- Basic Tests ---           01-07: ✓ All pass
--- Postconditions ---        08-09: ✓ All pass
--- Guards ---                10-11: ✓ All pass
--- Proof Persistence ---     12-14: ✓ All pass
--- Struct Fields ---         15-17: ✓ All pass
--- Z3 SMT ---                18-22: ✓ All pass
--- Multi-Arg Constraints --- 23-25: ✓ All pass
--- Switch Totality ---       26-27: ✓ All pass
--- Array Indices ---         28-32: ✓ All pass
--- old() Postconditions ---  33-35: ✓ All pass
--- Boolean Return ---        36-38: ✓ All pass
--- Method Call Tracking ---  39-46: ✓ All pass
--- Index Bounds ---          47-48: ✓ All pass
--- Symbolic Arguments ---    50-51: ✓ All pass
--- Loop Bounds ---           52-53: ✓ All pass
--- Arithmetic Expressions ---54-55: ✓ All pass
--- Transitive Proofs ---     56:    ✓ All pass
--- Index Arithmetic ---      57-58: ✓ All pass
--- Return Size ---           59-60: ✓ All pass
--- Symbolic Division ---     61:    ✓ All pass
--- Vector Count Ctor ---     62-63: ✓ All pass
--- Vector Assign ---         64-65: ✓ All pass
--- Copy/Move Assignment ---  66-67: ✓ All pass
--- Swap ---                  68-69: ✓ All pass
--- Move Semantics ---        70-71: ✓ All pass
--- Self-Assignment ---       72:    ✓ All pass
--- Complex Patterns ---      73-74: ✓ All pass
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
| **v16** | **Assignment & swap tracking, move invalidation** | **74** |

## Method Annotation Syntax

```cpp
// Query method — returns tracked property
[[clang::annotate("dtt:returns=_size")]]
size_t size() const;

// Precondition on this
[[clang::annotate("dtt:requires=_size > 0")]]
void pop();

// Postcondition on this
[[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
void push();

// Boolean return establishes proof
[[clang::annotate("dtt:ensures_if_true=_size == 0")]]
[[clang::annotate("dtt:ensures_if_false=_size > 0")]]
bool empty() const;

// Constructor postcondition (sets initial state)
[[clang::annotate("dtt:ensures=_size == 0")]]
Container() {}
```

## License

Public domain / CC0

## Index Bounds Checking (v11)

Compile-time detection of out-of-bounds array access:

\`\`\`cpp
dtt::vector<int> v;
v.push_back(1);  // _size = 1
v.push_back(2);  // _size = 2

v[0] = 10;  // ✓ OK: 0 < 2
v[1] = 20;  // ✓ OK: 1 < 2
v[2] = 30;  // ✗ ERROR: 2 < 2 is false
\`\`\`

**Error:**
\`\`\`
error: DTT: vector::operator[] index 2 out of bounds (requires < 2, but v._size = 2)
\`\`\`

**Annotation syntax:**
\`\`\`cpp
[[clang::annotate("dtt:requires=arg0 < _size")]]
reference operator[](size_type i);
\`\`\`

**Notes:**
- Only constant indices are checked at compile time
- Runtime indices (variables) are not checked
- Works with any tracked property, not just \`_size\`

\`\`\`
--- Basic Tests ---           01-07: ✓ All pass
--- Postconditions ---        08-09: ✓ All pass  
--- Guards ---                10-11: ✓ All pass
--- Proof Persistence ---     12-14: ✓ All pass
--- Struct Fields ---         15-17: ✓ All pass
--- Z3 SMT ---                18-22: ✓ All pass
--- Multi-Arg Constraints --- 23-25: ✓ All pass
--- Switch Totality ---       26-27: ✓ All pass
--- Array Indices ---         28-32: ✓ All pass
--- old() Postconditions ---  33-35: ✓ All pass
--- Boolean Return ---        36-38: ✓ All pass
--- Method Call Tracking ---  39-46: ✓ All pass
--- Index Bounds ---          47-48: ✓ All pass
\`\`\`

## Symbolic Argument Tracking (v12)

Method arguments can now be tracked symbolically in postconditions:

\`\`\`cpp
// Annotation
[[clang::annotate("dtt:ensures=_size == arg0")]]
void resize(size_type count);

[[clang::annotate("dtt:ensures=_size == old(_size) + arg1")]]
iterator insert(const_iterator pos, size_type count, const T& value);
\`\`\`

**Usage:**
\`\`\`cpp
dtt::vector<int> v;
v.resize(5);               // _size = 5 (constant)
v[4] = 10;                 // ✓ OK: 4 < 5

v.push_back(1);            // _size = 1
v.insert(v.begin(), 2, 0); // _size = 1 + 2 = 3
v[2] = 20;                 // ✓ OK: 2 < 3

v.assign(10, 42);          // _size = 10
v.front();                 // ✓ OK: _size > 0
\`\`\`

**Trackable operations:**

| Operation | Annotation | Result |
|-----------|------------|--------|
| \`resize(n)\` | \`_size == arg0\` | \`_size = n\` |
| \`assign(n, x)\` | \`_size == arg0\` | \`_size = n\` |
| \`insert(p, n, x)\` | \`_size == old(_size) + arg1\` | \`_size += n\` |

**Limitation:** When \`n\` is a runtime variable, guards on \`n\` don't automatically transfer to \`_size\`. This requires transitive proof propagation (future work).

## Test Results (50/50)

\`\`\`
--- Basic Tests ---           01-07: ✓ All pass
--- Postconditions ---        08-09: ✓ All pass  
--- Guards ---                10-11: ✓ All pass
--- Proof Persistence ---     12-14: ✓ All pass
--- Struct Fields ---         15-17: ✓ All pass
--- Z3 SMT ---                18-22: ✓ All pass
--- Multi-Arg Constraints --- 23-25: ✓ All pass
--- Switch Totality ---       26-27: ✓ All pass
--- Array Indices ---         28-32: ✓ All pass
--- old() Postconditions ---  33-35: ✓ All pass
--- Boolean Return ---        36-38: ✓ All pass
--- Method Call Tracking ---  39-46: ✓ All pass
--- Index Bounds ---          47-48: ✓ All pass
--- Symbolic Arguments ---    50-51: ✓ All pass
\`\`\`

## Arithmetic Expressions (v14)

Constraints can now use arbitrary arithmetic expressions with `+`, `-`, `*`:

```cpp
struct Buffer {
    [[clang::annotate("dtt:ensures=_size == arg0")]]
    Buffer(int n);
    
    // Copy range: start + len must be <= _size
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

Supported operators: `+`, `-`, `*`
Supported operands: `argN`, `_field`, `old(_field)`, constants

## Transitive Proofs (v14)

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

This enables safe patterns where runtime sizes are passed as parameters but guarded before use.

## Assignment & Swap Tracking (v16)

Properties are automatically tracked through assignment and swap operations:

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
std::swap(v1, v2);   // Also works with std::swap
```

**Tracked operations:**
- `v2 = v1` — copy assignment, v2 gets v1's properties
- `v2 = std::move(v1)` — move assignment, v2 gets v1's properties, v1 invalidated
- `v1.swap(v2)` — member swap, properties exchanged
- `std::swap(v1, v2)` — non-member swap, properties exchanged
- `v = v` — self-assignment safely ignored
