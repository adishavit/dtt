# DTT: Dependent Type Checking for C++ (v7)

A Clang plugin providing Idris-style dependent type checking with **Z3 SMT solver**, **multi-arg constraints**, and **switch totality**.

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
| **Multi-arg constraints** | ✓ NEW |
| **Switch totality** | ✓ NEW |

## Multi-Arg Constraints

Require relationships between multiple function arguments:

```cpp
[[clang::annotate("dtt:requires=arg0.len == arg1.len")]]
int dot(Vec& a, Vec& b);

Vec x, y;
x.len = 5;
y.len = 5;
dot(x, y);  // ✓ OK: both have len = 5

y.len = 3;
dot(x, y);  // ✗ ERROR: 5 != 3
```

## Switch Totality

Ensure all enum cases are handled:

```cpp
enum Color { Red, Green, Blue };

[[clang::annotate("dtt:total")]]
int colorCode(Color c) {
    switch (c) {
        case Red:   return 0;
        case Green: return 1;
        // Missing: Blue!
    }
    return -1;
}
// ✗ ERROR: switch on enum is not total — missing case: Blue
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

## Test Results (27/27)

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
  23_multiarg_pass.cpp:        ✓ PASS
  24_multiarg_fail.cpp:        ✓ PASS (rejected)
  25_multiarg_symbolic_pass:   ✓ PASS
--- Switch Totality ---
  26_switch_total_pass.cpp:    ✓ PASS
  27_switch_total_fail.cpp:    ✓ PASS (rejected)
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
| **v7** | **Multi-arg, Switch totality** | **27** |

## License

Public domain / CC0
