# DTT: Dependent Type Checking for C++

A Clang plugin that adds Idris-style dependent type checking to C++.

## What It Does

Tracks `.len` fields and enforces preconditions at **compile time**:

```cpp
#include "dtt.h"

int main() {
    Vec<int> v;
    v.len = 3;
    head(v);    // ✓ OK: 3 > 0
    
    Vec<int> empty;
    empty.len = 0;
    head(empty); // ✗ COMPILE ERROR: 0 > 0 is false
}
```

## Requirements

- Clang 14-18 (tested with 18)
- LLVM development headers

### Ubuntu/Debian
```bash
apt install clang-18 libclang-18-dev llvm-18-dev
```

### macOS
```bash
brew install llvm@18
```

## Build

```bash
chmod +x build.sh
./build.sh
```

Or manually:
```bash
clang++-18 -shared -fPIC \
    -o DTTPlugin.so \
    src/DTTPlugin.cpp \
    $(llvm-config-18 --cxxflags) \
    -I$(llvm-config-18 --includedir)
```

## Usage

```bash
clang++ -fplugin=./DTTPlugin.so -I./include your_file.cpp
```

## Features

### 1. Annotation-based length tracking

```cpp
DTT_LEN(5)
Vec<int> v;
head(v);  // OK: 5 > 0
```

### 2. Assignment tracking

```cpp
Vec<int> v;
v.len = 3;  // Plugin tracks this
head(v);    // OK: 3 > 0
```

### 3. Initializer list tracking (std::vector)

```cpp
std::vector<int> v = {1, 2, 3};  // Tracked: len = 3
head(v);  // OK
```

### 4. Branch refinement (runtime proofs)

```cpp
Vec<int> v;
v.len = argc;  // Runtime value — unknown

// head(v);  // ERROR: v.len is unknown

if (v.len > 0) {     // Guard acts as proof
    head(v);         // OK inside guard
}
```

## Defining Your Own Functions

```cpp
// Your function with precondition
[[clang::annotate("dtt:requires=len > 0")]]
int myHead(Vec<int>& v) {
    return v.data[0];
}

// Stricter precondition
[[clang::annotate("dtt:requires=len > 2")]]
int third(Vec<int>& v) {
    return v.data[2];
}
```

Supported constraints:
- `len > N`
- `len >= N`
- `len < N`
- `len <= N`
- `len == N`

## Comparison with Idris

| Feature | Idris | C++ + DTT Plugin |
|---------|-------|------------------|
| Compile-time type checking | ✓ | ✓ |
| `head` on empty = error | ✓ | ✓ |
| Runtime guards as proofs | ✓ | ✓ |
| Arbitrary dependent types | ✓ | ✗ (only .len) |
| Type-level computation | ✓ | ✗ |
| Proof terms as values | ✓ | ✗ |
| Totality checking | ✓ | ✗ |
| Inductively-defined proofs | ✓ | ✗ |
| Proof search / tactics | ✓ | ✗ |

## Limitations

1. **Only tracks `.len` field** — not arbitrary properties
2. **No arithmetic** — can't verify `append` produces `len = a + b`
3. **No cross-function tracking** — each function analyzed independently
4. **Guards must be simple** — only `if (v.len > 0)` pattern recognized
5. **No proof terms** — guards are implicit, not first-class
6. **No totality** — infinite loops still possible

## How It Works

```
Source Code → Clang Parser → AST → DTT Plugin → Errors/OK → Codegen
                                      ↓
                              Walks AST tracking:
                              - Variable declarations
                              - .len assignments  
                              - if (v.len > 0) guards
                              - Function calls with [dtt:requires]
```

The plugin is a **static analyzer** that runs inside the compiler.
It cannot track values that are only known at runtime unless
they're guarded by an `if` condition.

## Files

```
dtt_plugin_package/
├── CMakeLists.txt        # For CMake builds
├── build.sh              # Simple build script
├── README.md             # This file
├── src/
│   └── DTTPlugin.cpp     # Plugin source
├── include/
│   └── dtt.h             # User-facing header
└── examples/
    ├── 01_basic_pass.cpp     # Should compile
    ├── 02_basic_fail.cpp     # Should error
    ├── 03_runtime_guards.cpp # Should compile
    └── 04_runtime_fail.cpp   # Should error
```

## License

Public domain / CC0
