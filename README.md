# DTT: Dependent Type Checking for C++ (v9)

A Clang plugin providing Idris-style dependent type checking with Z3 SMT, multi-arg constraints, switch totality, array indices, **old() references**, and **boolean return postconditions**.

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
| **old() references** | ✓ NEW |
| **Boolean return postconditions** | ✓ NEW |

## old() References

Express postconditions relative to pre-call values:

```cpp
[[clang::annotate("dtt:ensures=result.size == old(arg0.size) + 1")]]
void push_back(Vec& v, int x);

Vec v;
v.size = 5;
push_back(v, 42);  // v.size = old(5) + 1 = 6

needsSix(v);  // ✓ OK: v.size = 6
```

**Chained calls work:**
```cpp
v.size = 5;
push_back(v, 1);  // v.size = 6
push_back(v, 2);  // v.size = 7
push_back(v, 3);  // v.size = 8
needsEight(v);    // ✓ OK
```

## Boolean Return Postconditions

Use function return values as proofs:

```cpp
[[clang::annotate("dtt:ensures_if_true=arg0.size == 0")]]
[[clang::annotate("dtt:ensures_if_false=arg0.size > 0")]]
bool empty(Vec& v);

Vec v;
if (!empty(v)) {
    // Proof established: v.size > 0
    head(v);  // ✓ OK!
}

if (empty(v)) {
    // Proof established: v.size == 0
    handleEmpty(v);  // ✓ OK!
}
```

## Requirements

```bash
apt install clang-18 libclang-18-dev llvm-18-dev z3
```

## Build & Test

```bash
chmod +x build.sh
./build.sh
```

## Test Results (38/38)

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
--- old() Postconditions ---
  33_old_pass.cpp:            ✓ PASS
  34_old_fail.cpp:            ✓ PASS (rejected)
  35_old_chain_pass.cpp:      ✓ PASS
--- Boolean Return ---
  36_bool_return_pass.cpp:    ✓ PASS
  37_bool_return_fail.cpp:    ✓ PASS (rejected)
  38_bool_true_branch_pass:   ✓ PASS
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
| **v9** | **old(), Boolean returns** | **38** |

## Syntax Reference

```cpp
// Preconditions
[[clang::annotate("dtt:requires=len > 0")]]
[[clang::annotate("dtt:requires=arg0.len == arg1.len")]]

// Postconditions (absolute)
[[clang::annotate("dtt:ensures=result.len == arg0.len - 1")]]

// Postconditions (relative to old value)
[[clang::annotate("dtt:ensures=result.size == old(arg0.size) + 1")]]

// Boolean return postconditions
[[clang::annotate("dtt:ensures_if_true=arg0.size == 0")]]
[[clang::annotate("dtt:ensures_if_false=arg0.size > 0")]]

// Switch totality
[[clang::annotate("dtt:total")]]

// Variable initialization
[[clang::annotate("dtt:len=5")]] Vec v;
```

## License

Public domain / CC0
