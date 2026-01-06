# DTT: Dependent Type Tracking for C++

[![CI](https://github.com/adishavit/dtt/actions/workflows/ci.yml/badge.svg)](https://github.com/adishavit/dtt/actions/workflows/ci.yml)

A Clang plugin providing compile-time dependent type checking with Z3 SMT solver. Track properties through code flow, prove bounds at compile time, catch errors before runtime.

## Quick Start

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install clang-18 llvm-18-dev libclang-18-dev cmake z3

# Build
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build

# Test (106 tests)
ctest --test-dir build

# Use
clang++-18 -fplugin=./build/libDTTPlugin.so your_file.cpp
```

## What It Does

DTT catches errors like this at **compile time**:

```cpp
#include "dtt_vector.h"

dtt::vector<int> v;
v.push_back(1);  // _size = 1
v.push_back(2);  // _size = 2

v[0] = 10;  // OK: 0 < 2
v[1] = 20;  // OK: 1 < 2
v[2] = 30;  // ERROR: index 2 out of bounds (requires < 2)
```

```
error: DTT: vector::operator[] index 2 out of bounds (requires < 2, but v._size = 2)
```

## Features

| Category | Features |
|----------|----------|
| **Tracking** | Any field, struct paths, copy/move/swap |
| **Constraints** | AND/OR, arithmetic (+,-,*,/), multi-arg |
| **Proofs** | Runtime guards, else-branches, transitive |
| **Contracts** | requires, ensures, old(), boolean returns |
| **Containers** | dtt::vector, std::array, std::span, std::bitset |
| **Solver** | Z3 SMT for complex symbolic reasoning |

## Integration

### Direct Usage
```bash
clang++ -fplugin=/path/to/libDTTPlugin.so -std=c++20 your_file.cpp
```

### CMake Project
```cmake
add_compile_options(-fplugin=/path/to/libDTTPlugin.so)
```

### Makefile
```makefile
CXXFLAGS += -fplugin=/path/to/libDTTPlugin.so
```

### IDE Support (clangd)

Generate `compile_commands.json`:
```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ...
ln -s build/compile_commands.json .
```

## Annotation Syntax

```cpp
struct Container {
    // Precondition
    [[clang::annotate("dtt:requires=_size > 0")]]
    void pop();

    // Postcondition
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push();

    // Return value tracking
    [[clang::annotate("dtt:returns=_size")]]
    size_t size() const;

    // Boolean branch proofs
    [[clang::annotate("dtt:ensures_if_true=_size == 0")]]
    [[clang::annotate("dtt:ensures_if_false=_size > 0")]]
    bool empty() const;

    // Multi-argument constraints
    [[clang::annotate("dtt:requires=arg0 + arg1 <= _size")]]
    void copy(int start, int len);
};
```

## dtt::vector\<T\>

Drop-in tracked wrapper for `std::vector<T>`:

```cpp
#include "dtt_vector.h"

dtt::vector<int> v;

v.push_back(1);     // _size = 1
v.resize(10);       // _size = 10
v[9] = 42;          // OK

if (!v.empty()) {
    v.pop_back();   // OK: !empty() proves _size > 0
}

v.clear();
v.front();          // ERROR: _size = 0
```

## Standard Library Support

DTT automatically tracks bounds for:

```cpp
std::array<int, 10> arr;
arr[9];   // OK
arr[10];  // ERROR: 10 >= 10

std::bitset<8> bits;
bits[7];  // OK
bits[8];  // ERROR: 8 >= 8

std::span<int, 5> sp(data);
sp[4];    // OK
sp[5];    // ERROR: 5 >= 5
```

## Building from Source

### Prerequisites

| Platform | Packages |
|----------|----------|
| **Ubuntu/Debian** | `clang-18 llvm-18-dev libclang-18-dev cmake z3` |
| **macOS** | `brew install llvm@18 cmake z3` |
| **Arch** | `clang llvm cmake z3` |

### Build Steps

```bash
# Configure
cmake -B build \
  -DCMAKE_C_COMPILER=clang-18 \
  -DCMAKE_CXX_COMPILER=clang++-18

# Build
cmake --build build

# Test
ctest --test-dir build

# Install (optional)
sudo cmake --install build
```

### macOS Notes

Use Homebrew LLVM path:
```bash
cmake -B build \
  -DCMAKE_C_COMPILER=$(brew --prefix llvm@18)/bin/clang \
  -DCMAKE_CXX_COMPILER=$(brew --prefix llvm@18)/bin/clang++ \
  -DLLVM_DIR=$(brew --prefix llvm@18)/lib/cmake/llvm \
  -DClang_DIR=$(brew --prefix llvm@18)/lib/cmake/clang
```

## Test Suite

106 tests covering:
- Basic tracking and constraints
- Postconditions and guards
- Proof persistence and copying
- Z3 SMT solver integration
- Method call tracking
- Index bounds checking
- Assignment and swap tracking
- Template parameter extraction
- Standard library containers
- Equality/inequality constraints
- Division and multiplication

Run specific tests:
```bash
ctest --test-dir build -R "z3"        # Z3-related tests
ctest --test-dir build -R "_pass"     # All pass tests
ctest --test-dir build --verbose      # Verbose output
```

## Documentation

See [GUIDE.md](GUIDE.md) for detailed documentation:
- Full feature list
- Annotation reference
- Advanced examples
- Version history

## License

BSD 2-Clause "Simplified" License. See [LICENSE](LICENSE).
