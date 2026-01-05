# DTT: Dependent Type Checking for C++ (v6)

A Clang plugin providing Idris-style dependent type checking with **Z3 SMT solver integration**.

## Features

| Feature | Status |
|---------|--------|
| Track any field | ✓ |
| Compound constraints | ✓ |
| Symbolic arithmetic | ✓ |
| Runtime guards as proofs | ✓ |
| Postconditions | ✓ |
| Else-branch guards | ✓ |
| Proof persistence/copying | ✓ |
| Struct field paths | ✓ |
| **Z3 SMT solver** | ✓ NEW |

## Z3 Integration

When simple evaluation fails, the plugin falls back to Z3 for arbitrary arithmetic proofs:

```cpp
Vec v;
v.len = getValue();  // Unknown runtime value

if (v.len > 10) {
    needsFive(v);    // ✓ Z3 proves: (len > 10) → (len >= 5)
}
```

**Chained arithmetic:**

```cpp
Vec a; a.len = 10;
Vec b = tail(a);   // b.len = 9
Vec c = tail(b);   // c.len = 8
Vec d = tail(c);   // d.len = 7
...
Vec g = tail(f);   // g.len = 4

needsThree(g);     // ✓ Z3 proves: 4 >= 3
```

## Requirements

```bash
# Ubuntu/Debian
apt install clang-18 libclang-18-dev llvm-18-dev z3

# macOS
brew install llvm@18 z3
```

## Build & Test

```bash
chmod +x build.sh
./build.sh
```

## Test Results (22/22)

```
  01_basic_pass.cpp:          ✓ PASS
  02_basic_fail.cpp:          ✓ PASS (rejected)
  03_runtime_guards.cpp:      ✓ PASS
  04_runtime_fail.cpp:        ✓ PASS (rejected)
  05_any_property.cpp:        ✓ PASS
  06_compound_constraints:    ✓ PASS
  07_arithmetic.cpp:          ✓ PASS
  08_postcond_pass.cpp:       ✓ PASS
  09_postcond_fail.cpp:       ✓ PASS (rejected)
  10_else_guard_pass.cpp:     ✓ PASS
  11_else_guard_fail.cpp:     ✓ PASS (rejected)
  12_proof_persist.cpp:       ✓ PASS
  13_proof_copy.cpp:          ✓ PASS
  14_proof_invalidate.cpp:    ✓ PASS (rejected)
  15_struct_field_pass.cpp:   ✓ PASS
  16_struct_field_fail.cpp:   ✓ PASS (rejected)
  17_nested_struct_pass.cpp:  ✓ PASS
  --- Z3 Tests ---
  18_z3_arithmetic_pass.cpp:  ✓ PASS
  19_z3_arithmetic_fail.cpp:  ✓ PASS (rejected)
  20_z3_symbolic_pass.cpp:    ✓ PASS
  21_z3_chain_pass.cpp:       ✓ PASS
  22_z3_chain_fail.cpp:       ✓ PASS (rejected)
```

## How Z3 Integration Works

```
Constraint Check
      │
      ▼
┌──────────────┐
│ Simple eval? │──yes──▶ Return result
└──────────────┘
      │ no
      ▼
┌──────────────┐
│ Generate SMT │
│ - declare vars
│ - assert known values
│ - assert known proofs  
│ - assert NOT(required)
└──────────────┘
      │
      ▼
┌──────────────┐
│ Run Z3       │
└──────────────┘
      │
      ▼
  ┌───┴───┐
  │       │
unsat    sat
  │       │
  ▼       ▼
PROVEN   DISPROVEN
```

**Example SMT generated:**

```smt2
(set-logic QF_LIA)
(declare-const v_len Int)
(assert (> v_len 10))        ; Known proof from guard
(assert (not (>= v_len 5)))  ; Negation of required
(check-sat)
; Result: unsat → constraint must hold
```

## Version History

| Version | Features | Tests |
|---------|----------|-------|
| v1 | Basic tracking | 4 |
| v2 | Any property, AND/OR | 7 |
| v3 | Postconditions, else-guards | 11 |
| v4 | Proof persistence | 14 |
| v5 | Struct field paths | 17 |
| **v6** | **Z3 SMT solver** | **22** |

## Comparison with Idris

| Feature | Idris | C++ + DTT v6 |
|---------|-------|--------------|
| Compile-time checking | ✓ | ✓ |
| Preconditions | ✓ | ✓ |
| Postconditions | ✓ | ✓ |
| Arithmetic proofs | ✓ | ✓ (Z3) |
| Guard refinement | ✓ | ✓ |
| Proof persistence | ✓ | ✓ |
| Nested access | ✓ | ✓ |
| **SMT proving** | ✓ | ✓ |
| Dependent return types | ✓ | Partial |
| Inductive proofs | ✓ | ✗ |
| Proof search | ✓ | Partial (Z3) |

## License

Public domain / CC0
