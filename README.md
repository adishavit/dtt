# DTT: Dependent Type Checking for C++ (v12)

A Clang plugin providing Idris-style dependent type checking with Z3 SMT, method call tracking, index bounds checking, **symbolic argument tracking**, and a tracked `dtt::vector<T>` wrapper.

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
| **Symbolic argument tracking** | ✓ NEW |

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

## Test Results (46/46)

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
| **v10** | **Method tracking, dtt::vector** | **46** |

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
