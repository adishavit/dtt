# DTT: Dependent Type Checking for C++ (v8)

A Clang plugin providing Idris-style dependent type checking with **Z3 SMT**, **multi-arg constraints**, **switch totality**, and **constant array indices**.

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
| **Constant array indices** | ✓ NEW |

## Constant Array Indices

Track properties on individual array elements:

```cpp
Vec arr[3];
arr[0].len = 5;
arr[1].len = 0;
arr[2].len = 7;

head(arr[0]);  // ✓ OK: arr[0].len = 5 > 0
head(arr[2]);  // ✓ OK: arr[2].len = 7 > 0
head(arr[1]);  // ✗ ERROR: arr[1].len = 0
```

**Runtime index invalidates all elements:**

```cpp
arr[0].len = 5;
arr[i].len = 0;   // i is runtime — invalidates ALL arr[*].len
head(arr[0]);     // ✗ ERROR: arr[0].len is now unknown
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

## Test Results (32/32)

```
--- Basic Tests ---
  01-07: ✓ All pass
--- Postconditions ---
  08-09: ✓ All pass  
--- Guards ---
  10-11: ✓ All pass
--- Proof Persistence ---
  12-14: ✓ All pass
--- Struct Fields ---
  15-17: ✓ All pass
--- Z3 SMT ---
  18-22: ✓ All pass
--- Multi-Arg Constraints ---
  23-25: ✓ All pass
--- Switch Totality ---
  26-27: ✓ All pass
--- Array Indices ---
  28_array_const_pass.cpp:           ✓ PASS
  29_array_const_fail.cpp:           ✓ PASS (rejected)
  30_array_guard_pass.cpp:           ✓ PASS
  31_array_runtime_invalidate.cpp:   ✓ PASS (rejected)
  32_array_different_indices_pass:   ✓ PASS
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
| **v8** | **Constant array indices** | **32** |

## How Array Tracking Works

```cpp
// Path generation for array subscripts:
arr[0].len  →  "arr[0].len"   (trackable)
arr[1].len  →  "arr[1].len"   (trackable)
arr[i].len  →  "arr[?].len"   (runtime — not tracked)

// Writing to runtime index invalidates entire array:
arr[i].len = 0;  
// Removes: arr[0].len, arr[1].len, arr[2].len, ...
// Removes: arr[0]._proof_*, arr[1]._proof_*, ...
```

## Comparison with Idris

| Feature | Idris | DTT v8 |
|---------|-------|--------|
| `arr[0]` (constant index) | ✓ | ✓ |
| `arr[i]` (runtime index) | ✓ | ✗ (conservative) |
| Index bounds via types | ✓ (`Fin n`) | ✗ |
| Proof that `i ≠ j` | ✓ | Partial (via Z3) |

## License

Public domain / CC0
