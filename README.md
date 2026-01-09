# DTT: Dependent Type Tracking for C++

[![CI](https://github.com/adishavit/dtt/actions/workflows/ci.yml/badge.svg)](https://github.com/adishavit/dtt/actions/workflows/ci.yml)

**Catch CVE-class bugs at compile time.** Buffer overflows, null pointer dereferences, use-after-move — DTT proves your code safe or shows you exactly where it's not.

## The Bugs That Ship

This compiles. It passes code review. It ships. Then it gets a CVE number.

```cpp
#include "dtt_vector.h"  // drop-in std::vector replacement
using dtt::vector;

void vulnerable(int* session) {
    vector<int> data(2);              // _size = 2
    data.pop_back();                  // _size = 1
    data.pop_back();                  // _size = 0
    int first = data.front();         // BUG 1: empty container

    if (rand() % 2)
        session = nullptr;
    *session = first;                 // BUG 2: null dereference

    vector<int> backup = std::move(data);
    data[0] = 999;                    // BUG 3: use after move
}
```

**Without DTT**: Compiles clean. All 3 bugs ship to production.

## With DTT

```
error: DTT: vector::front requires _size > 0, but data._size = 0
   10 |     int first = data.front();
      |                 ^

error: DTT: potential null pointer dereference of 'session' (known to be null)
   14 |     *session = first;
      |     ^

error: DTT: vector::operator[] index 0 out of bounds (requires < 0, but data._size = 0)
   17 |     data[0] = 999;
      |     ^
```

| Bug | Class | Real-World Impact |
|-----|-------|-------------------|
| **#1** | Empty container access | Heartbleed (buffer over-read), countless others |
| **#2** | Null pointer dereference | 70% of Chrome/Firefox security bugs |
| **#3** | Use after move/free | Use-after-free: top memory corruption CVE class |

**3 bugs. 3 errors. Zero runtime cost.**

## How It Works

DTT is a Clang plugin that tracks *semantic properties* through your code:

```cpp
dtt::vector<int> v;        // DTT knows: v._size = 0
v.push_back(1);            // DTT knows: v._size = 1
v.push_back(2);            // DTT knows: v._size = 2

v[0] = 10;                 // OK: 0 < 2 ✓
v[1] = 20;                 // OK: 1 < 2 ✓
v[2] = 30;                 // ERROR: 2 < 2 ✗
```

When you call a method with a precondition, DTT *proves* it's satisfied — or shows you the counterexample.

## Under The Hood

`dtt::vector` is just `std::vector` with annotations:

```cpp
// Inside dtt_vector.h
template<typename T>
class vector : public std::vector<T> {
    // ...

    [[clang::annotate("dtt:requires=_size > 0")]]
    reference front() { return Base::front(); }

    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push_back(const T& value) { Base::push_back(value); }

    [[clang::annotate("dtt:ensures=_size == 0")]]
    void clear() { Base::clear(); }

    // ... 30+ annotated methods
};
```

You can annotate your own types the same way.

## Quick Start

```bash
# Ubuntu/Debian
sudo apt install clang-18 llvm-18-dev libclang-18-dev cmake libz3-dev

# Build
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build

# Try it
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 your_file.cpp
```

## What DTT Catches

| Bug Class | Example | CVE Examples |
|-----------|---------|--------------|
| **Buffer overflow** | `v[v.size()]` | Heartbleed, countless others |
| **Empty container access** | `v.front()` on empty | Container UB bugs |
| **Null dereference** | `*ptr` when maybe-null | Chrome, Firefox, everywhere |
| **Use after move** | Access moved-from object | C++ footgun |
| **Integer overflow in index** | `v[i + j]` without bounds | Integer overflow CVEs |

## What DTT Tracks

| Category | Capabilities |
|----------|-------------|
| **Properties** | Any field: `_size`, `_capacity`, `_nonnull`, custom |
| **Flow** | Assignments, copies, moves, swaps |
| **Proofs** | `if (x > 0)` proves `x > 0` in the branch |
| **Contracts** | `requires`, `ensures`, `old()`, boolean returns |
| **Solver** | Z3 SMT for symbolic arithmetic: `v[i + j * 2]` |

## Runtime Guards Work

```cpp
dtt::vector<int> v = get_data();  // _size unknown

if (!v.empty()) {
    v.front();    // OK: !empty() proves _size > 0
}

v.front();        // ERROR: _size unknown here
```

## Annotate Your Own Types

```cpp
struct Buffer {
    char* data;
    int _size;
    int _capacity;

    [[clang::annotate("dtt:requires=_size < _capacity")]]
    void append(char c) {
        data[_size++] = c;
    }

    [[clang::annotate("dtt:requires=index < _size")]]
    char& operator[](int index) {
        return data[index];
    }
};
```

## Standard Library Support

DTT automatically tracks bounds for:

```cpp
std::array<int, 10> arr;
arr[10];  // ERROR: 10 >= 10

std::span<int, 5> sp(data);
sp[5];    // ERROR: 5 >= 5

std::bitset<8> bits;
bits[8];  // ERROR: 8 >= 8
```

## Three Modes

| Mode | Category C/D | Use Case |
|------|--------------|----------|
| `inform` | Warnings | Default: see issues without blocking |
| `enforce` | Errors | CI: fail on any uncertainty |
| `quiet` | Silent | Gradual adoption |

```bash
clang++ -fplugin=./libDTTPlugin.so -fplugin-arg-dtt---dtt-mode=enforce ...
```

## Philosophy

**DTT errors mean "I found a bug", not "I couldn't prove safety".**

When DTT can't reason about something (lost tracking, solver timeout), it warns by default — not errors. You're never forced to refactor working code just to satisfy the checker.

See [PHILOSOPHICAL_DESIGN_CONCERNS.md](PHILOSOPHICAL_DESIGN_CONCERNS.md) for the full design rationale.

## Test Suite

186 tests covering bounds checking, nullptr tracking, contract propagation, Z3 integration, and edge cases:

```bash
ctest --test-dir build                    # Run all
ctest --test-dir build -R "nullptr"       # Nullptr tests
ctest --test-dir build -L "xfail"         # Known limitations
```

## Documentation

- [GUIDE.md](GUIDE.md) — Full feature reference
- [PHILOSOPHICAL_DESIGN_CONCERNS.md](PHILOSOPHICAL_DESIGN_CONCERNS.md) — Design philosophy

## Requirements

| Platform | Packages |
|----------|----------|
| **Ubuntu/Debian** | `clang-18 llvm-18-dev libclang-18-dev cmake libz3-dev` |
| **macOS** | `brew install llvm@18 cmake z3` |
| **Arch** | `clang llvm cmake z3` |

## License

BSD 2-Clause. See [LICENSE](LICENSE).
