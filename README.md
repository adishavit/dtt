# DTT: Dependent Type Checking for C++ (v2)

A Clang plugin that adds Idris-style dependent type checking to C++.

## Features (v2)

| Feature | Status |
|---------|--------|
| Track `.len` field | ✓ |
| Track **any** field | ✓ NEW |
| Simple constraints (`len > 0`) | ✓ |
| Compound constraints (`size > 0 && capacity >= 10`) | ✓ NEW |
| Symbolic arithmetic (`c.len = a.len + b.len`) | ✓ NEW |
| Runtime guards as proofs | ✓ |
| Compile-time errors | ✓ |

## Quick Example

```cpp
struct Buffer { int size; int capacity; };

// Function requires BOTH conditions
[[clang::annotate("dtt:requires=size > 0 && capacity >= 10")]]
void process(Buffer& b) { ... }

int main() {
    Buffer b;
    b.size = 5;
    b.capacity = 20;
    
    process(b);  // ✓ OK: both conditions met
    
    Buffer small;
    small.size = 1;
    small.capacity = 5;
    
    process(small);  // ✗ COMPILE ERROR: capacity < 10
}
```

## Requirements

```bash
# Ubuntu/Debian
apt install clang-18 libclang-18-dev llvm-18-dev

# macOS  
brew install llvm@18
```

## Build & Test

```bash
chmod +x build.sh
./build.sh
```

Output:
```
=== Running Tests ===
  01_basic_pass.cpp: ✓ PASS
  02_basic_fail.cpp: ✓ PASS (rejected)
  03_runtime_guards.cpp: ✓ PASS
  04_runtime_fail.cpp: ✓ PASS (rejected)
  05_any_property.cpp: ✓ PASS
  06_compound_constraints.cpp: ✓ PASS
  07_arithmetic.cpp: ✓ PASS
```

## Usage

```bash
clang++ -fplugin=./DTTPlugin.so your_file.cpp
```

## Annotation Reference

### Variable annotations

```cpp
// Track any field with known value
[[clang::annotate("dtt:len=5")]]
Vec v;

[[clang::annotate("dtt:size=10")]]
Buffer b;

[[clang::annotate("dtt:capacity=100")]]
Buffer big;
```

### Function preconditions

```cpp
// Simple
[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

// Any field
[[clang::annotate("dtt:requires=capacity >= 100")]]
void needsBig(Buffer& b);

// Compound (AND)
[[clang::annotate("dtt:requires=size > 0 && capacity >= 10")]]
void process(Buffer& b);

// Supported operators: > >= < <= == !=
```

### Automatic tracking

```cpp
// Assignment tracking
Buffer b;
b.size = 5;       // Plugin tracks: b.size = 5
b.capacity = 20;  // Plugin tracks: b.capacity = 20

// Initializer list (std::vector, etc.)
std::vector<int> v = {1, 2, 3};  // Plugin tracks: v.len = 3

// Arithmetic
Vec c;
c.len = a.len + b.len;  // Plugin tracks: c.len = a.len + b.len
```

### Runtime guards

```cpp
Buffer b;
b.capacity = getUserInput();  // Unknown at compile time

// ERROR without guard:
// process(b);  

// OK with guard:
if (b.capacity >= 10) {
    process(b);  // ✓ Guard acts as proof
}
```

## Comparison with Idris

| Feature | Idris | C++ + DTT v2 |
|---------|-------|--------------|
| Compile-time checking | ✓ | ✓ |
| Any property | ✓ | ✓ |
| Compound constraints | ✓ | ✓ |
| Type-level arithmetic | ✓ | ✓ (limited) |
| Runtime guards as proofs | ✓ | ✓ |
| Dependent return types | ✓ | ✗ |
| Inductively-defined proofs | ✓ | ✗ |
| Totality checking | ✓ | ✗ |
| Proof search | ✓ | ✗ |

## Files

```
dtt_plugin_package/
├── build.sh              # Build + test
├── CMakeLists.txt        # CMake alternative
├── README.md
├── src/
│   └── DTTPlugin.cpp     # Plugin (~450 lines)
├── include/
│   └── dtt.h             # Helper macros
└── examples/
    ├── 01_basic_pass.cpp
    ├── 02_basic_fail.cpp
    ├── 03_runtime_guards.cpp
    ├── 04_runtime_fail.cpp
    ├── 05_any_property.cpp
    ├── 06_compound_constraints.cpp
    └── 07_arithmetic.cpp
```

## How It Works

```
Source → Clang Parser → AST → DTT Plugin → Errors/OK → Codegen
                               │
                               ├─ Track variable declarations
                               ├─ Track field assignments (v.len = N)
                               ├─ Track arithmetic (c.len = a.len + b.len)
                               ├─ Track if-guards (if v.len > 0)
                               └─ Check function preconditions
```

## License

Public domain / CC0
