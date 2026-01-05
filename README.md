# DTT: Dependent Type Checking for C++ (v5)

A Clang plugin providing Idris-style dependent type checking for C++.

## Features

| Feature | Status |
|---------|--------|
| Track any field (.len, .size, .capacity) | ✓ |
| Simple constraints (`len > 0`) | ✓ |
| Compound constraints (`size > 0 && capacity >= 10`) | ✓ |
| Symbolic arithmetic (`c.len = a.len + b.len`) | ✓ |
| Runtime guards as proofs | ✓ |
| Postconditions | ✓ |
| Else-branch guards | ✓ |
| Proof persistence | ✓ |
| Proof copying | ✓ |
| Proof invalidation on mutation | ✓ |
| **Struct field paths** (`p.inventory.len`) | ✓ NEW |

## Quick Example

```cpp
struct Vec { int* data; int len; };
struct Player { Vec inventory; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main(int argc, char**) {
    Player p;
    p.inventory.len = argc;  // Runtime
    
    if (p.inventory.len > 0) {
        head(p.inventory);   // ✓ OK: proof for p.inventory
    }
    
    head(p.inventory);       // ✗ ERROR: no proof outside guard
}
```

Error message:
```
error: DTT: head requires len > 0, but p.inventory.len is unknown (no proof)
note: add guard: if (p.inventory.len > 0) { ... }
```

## Build & Test

```bash
apt install clang-18 libclang-18-dev llvm-18-dev
chmod +x build.sh
./build.sh
```

## Test Results (17/17)

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
12_proof_persist.cpp:     ✓ PASS
13_proof_copy.cpp:        ✓ PASS
14_proof_invalidate.cpp:  ✓ PASS (rejected)
15_struct_field_pass.cpp: ✓ PASS      ← NEW
16_struct_field_fail.cpp: ✓ PASS      ← NEW
17_nested_struct_pass.cpp:✓ PASS      ← NEW
```

## Key Implementation: Path Tracking

```cpp
// Recursively builds path string
std::string getExprPath(const Expr *E) {
    if (auto *DRE = dyn_cast<DeclRefExpr>(E))
        return DRE->getDecl()->getNameAsString();
    
    if (auto *ME = dyn_cast<MemberExpr>(E))
        return getExprPath(ME->getBase()) + "." + 
               ME->getMemberDecl()->getNameAsString();
    
    return "";
}

// s.v.len -> "s.v.len"
// p.inventory -> "p.inventory"
// g.player.inv.items -> "g.player.inv.items"
```

## Comparison with Idris

| Feature | Idris | C++ + DTT v5 |
|---------|-------|--------------|
| Compile-time checking | ✓ | ✓ |
| Preconditions | ✓ | ✓ |
| Postconditions | ✓ | ✓ |
| Symbolic arithmetic | ✓ | ✓ |
| Runtime guards | ✓ | ✓ |
| Proof persistence | ✓ | ✓ |
| Nested field access | ✓ | ✓ |
| Proof through aliasing | ✓ | ✗ |
| Dependent return types | ✓ | Partial |
| Inductive proofs | ✓ | ✗ |
| Proof search | ✓ | ✗ |

## License

Public domain / CC0
