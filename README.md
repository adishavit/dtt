# DTT: Dependent Type Checking for C++ (v3)

A Clang plugin providing Idris-style dependent type checking for C++.

## Features

| Feature | Status |
|---------|--------|
| Track any field (.len, .size, .capacity) | ✓ |
| Simple constraints (`len > 0`) | ✓ |
| Compound constraints (`size > 0 && capacity >= 10`) | ✓ |
| Symbolic arithmetic (`c.len = a.len + b.len`) | ✓ |
| Runtime guards as proofs (`if (v.len > 0)`) | ✓ |
| **Postconditions** (`ensures=result.len == arg0.len - 1`) | ✓ NEW |
| **Else-branch guards** (negated conditions) | ✓ NEW |
| Compile-time errors | ✓ |

## Quick Example

```cpp
struct Vec { int* data; int len; };

// tail: removes first element
[[clang::annotate("dtt:requires=len > 0")]]
[[clang::annotate("dtt:ensures=result.len == arg0.len - 1")]]
Vec tail(Vec& v);

[[clang::annotate("dtt:requires=len >= 3")]]
void needsThree(Vec& v);

int main() {
    Vec a;
    a.len = 5;
    
    Vec b = tail(a);  // Plugin tracks: b.len = 4
    Vec c = tail(b);  // Plugin tracks: c.len = 3
    
    needsThree(c);    // ✓ OK: 3 >= 3
    
    Vec d = tail(c);  // d.len = 2
    needsThree(d);    // ✗ COMPILE ERROR: 2 >= 3 is false
}
```

## Build & Test

```bash
# Requirements (Ubuntu/Debian)
apt install clang-18 libclang-18-dev llvm-18-dev

# Build and test
chmod +x build.sh
./build.sh
```

## Annotation Reference

### Variable annotations
```cpp
[[clang::annotate("dtt:len=5")]]    Vec v;
[[clang::annotate("dtt:size=10")]]  Buffer b;
```

### Function preconditions
```cpp
[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

[[clang::annotate("dtt:requires=size > 0 && capacity >= 10")]]
void process(Buffer& b);
```

### Function postconditions (NEW)
```cpp
[[clang::annotate("dtt:ensures=result.len == arg0.len - 1")]]
Vec tail(Vec& v);

[[clang::annotate("dtt:ensures=result.len == arg0.len + arg1.len")]]
Vec concat(Vec& a, Vec& b);

[[clang::annotate("dtt:ensures=result.len == 0")]]
Vec empty();
```

### Runtime guards with else (NEW)
```cpp
Vec v;
v.len = getUserInput();  // Unknown

if (v.len > 0) {
    head(v);        // ✓ OK: len > 0 proven
} else {
    handleEmpty(v); // ✓ OK: len <= 0 proven in else
}
```

## Test Results

```
01_basic_pass.cpp:        ✓ PASS
02_basic_fail.cpp:        ✓ PASS (rejected)
03_runtime_guards.cpp:    ✓ PASS
04_runtime_fail.cpp:      ✓ PASS (rejected)
05_any_property.cpp:      ✓ PASS
06_compound_constraints:  ✓ PASS
07_arithmetic.cpp:        ✓ PASS
08_postcond_pass.cpp:     ✓ PASS
09_postcond_fail.cpp:     ✓ PASS (rejected)
10_else_guard_pass.cpp:   ✓ PASS
11_else_guard_fail.cpp:   ✓ PASS (rejected)
```

## Files

```
dtt_plugin_package/
├── build.sh                      # Build + test
├── README.md
├── src/DTTPlugin.cpp             # Plugin (~500 lines)
├── include/dtt.h                 # Helper macros
└── examples/
    ├── 01_basic_pass.cpp
    ├── 02_basic_fail.cpp
    ├── 03_runtime_guards.cpp
    ├── 04_runtime_fail.cpp
    ├── 05_any_property.cpp
    ├── 06_compound_constraints.cpp
    ├── 07_arithmetic.cpp
    ├── 08_postcond_pass.cpp      # NEW
    ├── 09_postcond_fail.cpp      # NEW
    ├── 10_else_guard_pass.cpp    # NEW
    └── 11_else_guard_fail.cpp    # NEW
```

## Comparison with Idris

| Feature | Idris | C++ + DTT v3 |
|---------|-------|--------------|
| Compile-time checking | ✓ | ✓ |
| Preconditions | ✓ | ✓ |
| Postconditions | ✓ | ✓ |
| Symbolic arithmetic | ✓ | ✓ |
| Runtime guards | ✓ | ✓ |
| Else-branch refinement | ✓ | ✓ |
| Dependent return types | ✓ | Partial |
| Inductive proofs | ✓ | ✗ |
| Totality | ✓ | ✗ |
| Proof search | ✓ | ✗ |

## License

Public domain / CC0
