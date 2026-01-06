# Catching Buffer Overflows at Compile Time: Introducing DTT

**TL;DR:** DTT is a Clang plugin that brings Idris-style dependent type checking to C++, catching out-of-bounds access, empty container errors, and use-after-move bugs at compile time — before your code ever runs.

---

## The Problem: Memory Safety is Still Eating Us Alive

It's 2024, and we're still writing CVEs about buffer overflows. Microsoft reports that 70% of their security vulnerabilities are memory safety issues. Google says the same about Chrome. The CWE Top 25 is dominated by CWE-787 (Out-of-bounds Write) and CWE-125 (Out-of-bounds Read).

We have tools: AddressSanitizer catches bugs at runtime. Fuzzing finds edge cases. Code review catches some issues. But these are all *reactive* — they find bugs after you've written them, often after they've shipped.

What if we could catch these bugs at compile time?

## Enter Dependent Types

In languages like Idris, Agda, and Coq, types can *depend on values*. You don't just have `Vector<int>` — you have `Vector<int, 5>`, a vector that the type system *knows* has exactly 5 elements. Try to access index 6? Compile error. Not a runtime crash. Not a CVE. A compile error.

This sounds great, but C++ isn't Idris. We can't just rewrite millions of lines of production code in a dependently-typed language.

Or can we fake it?

## DTT: Dependent Type Tracking for C++

DTT is a Clang plugin that tracks value-dependent properties through your code and verifies constraints at compile time. It doesn't require language changes — just annotations.

Here's the basic idea:

```cpp
#include "dtt_vector.h"

void example() {
    dtt::vector<int> v;      // DTT knows: _size = 0

    v.push_back(1);          // DTT knows: _size = 1
    v.push_back(2);          // DTT knows: _size = 2

    v[0] = 10;               // ✓ OK: 0 < 2
    v[1] = 20;               // ✓ OK: 1 < 2
    v[2] = 30;               // ✗ COMPILE ERROR: 2 < 2 is false
}
```

That last line doesn't compile. Not because of a template trick or a runtime check — because DTT *proved* at compile time that the access is out of bounds.

## How It Works

DTT tracks symbolic values through your program. When you call `push_back()`, it knows `_size` increased by 1. When you call `resize(10)`, it knows `_size` is now 10. When you access `v[i]`, it checks whether it can prove `i < _size`.

The magic is in the annotations:

```cpp
template<typename T>
class vector {
    [[clang::annotate("dtt:ensures=_size == 0")]]
    vector();

    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push_back(const T& value);

    [[clang::annotate("dtt:requires=_size > 0")]]
    [[clang::annotate("dtt:ensures=_size == old(_size) - 1")]]
    void pop_back();

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    T& operator[](size_t i);
};
```

These annotations form contracts: preconditions (`requires`), postconditions (`ensures`), and queries (`returns`). DTT tracks these through your code and catches violations.

## Zero Runtime Overhead

Here's the crucial part: **the generated code is identical to unchecked code**.

DTT is a pure static analysis tool. The annotations are only read by the plugin during compilation — they generate no runtime code. The `dtt::vector<T>` wrapper compiles down to exactly the same machine code as `std::vector<T>`. No bounds-checking branches. No debug assertions. No performance penalty.

This is fundamentally different from runtime tools like AddressSanitizer or debug iterators. Those add overhead (sometimes 2-10x slowdown) and are typically disabled in production. DTT's checks happen entirely at compile time — if your code compiles, the checks have passed, and the resulting binary runs at full speed.

You get the safety of bounds checking with the performance of unchecked code.

## But What About Runtime Values?

Here's where it gets interesting. DTT handles runtime values through *guards*:

```cpp
void process(dtt::vector<int>& v, int i) {
    // i is unknown at compile time

    v[i] = 42;  // ✗ ERROR: can't prove i < _size

    if (i < v.size()) {
        v[i] = 42;  // ✓ OK: guard proves i < _size
    }
}
```

When you write a bounds check, DTT *learns* from it. Inside the `if` block, it has proof that `i < v.size()`. This is exactly the pattern you'd write anyway for safe code — DTT just verifies you actually wrote it.

## Loops Work Too

```cpp
void fill(dtt::vector<int>& v) {
    for (int i = 0; i < v.size(); i++) {
        v[i] = i * 2;  // ✓ OK: loop condition proves i < _size
    }
}
```

DTT understands that the loop condition `i < v.size()` holds for every iteration of the loop body. No annotations needed.

## The Z3 Secret Weapon

For complex constraints, DTT calls out to the Z3 SMT solver:

```cpp
struct Buffer {
    [[clang::annotate("dtt:requires=arg0 + arg1 <= _size")]]
    void copyRange(int start, int len);
};

Buffer buf(10);         // _size = 10
buf.copyRange(3, 7);    // ✓ OK: 3 + 7 = 10 <= 10
buf.copyRange(5, 6);    // ✗ ERROR: 5 + 6 = 11 > 10
```

When the arithmetic gets complex, DTT generates an SMT formula and asks Z3 to prove (or disprove) it. This handles transitive reasoning, symbolic arithmetic, and complex constraint combinations.

## Assignment and Move Tracking

DTT tracks properties through assignment and swap operations:

```cpp
dtt::vector<int> v1, v2;
v1.resize(5);           // v1._size = 5

v2 = v1;                // v2._size = 5 (copied)
v2[4];                  // ✓ OK

v2 = std::move(v1);     // v2._size = 5, v1 = UNKNOWN
v1.front();             // ✗ ERROR: v1._size is unknown (moved-from!)
```

Use-after-move is a subtle bug that's easy to introduce and hard to catch. DTT catches it at compile time by marking moved-from objects as having unknown state.

## What DTT Catches

| Bug Class | How DTT Catches It |
|-----------|-------------------|
| Buffer overflow | `requires arg0 < _size` on `operator[]` |
| Empty container access | `requires _size > 0` on `front()`, `back()`, `pop_back()` |
| Off-by-one errors | Exact symbolic tracking of `_size` |
| Use-after-move | Move invalidation marks source as Unknown |
| Missing bounds checks | Rejects code without provable guards |

## What DTT Doesn't Catch

Let's be honest about limitations:

- **Runtime-only violations**: If the bug only manifests with specific runtime input that DTT can't reason about, it won't catch it
- **Logic errors**: DTT verifies contracts, not correctness
- **Unannotated code**: Only works with annotated types or `dtt::vector`
- **C code**: This is a C++ Clang plugin
- **Complex aliasing**: `v1` and `v2` pointing to the same vector will confuse tracking

## The Bigger Picture

DTT is a proof of concept, but the approach is sound. Rust's borrow checker is essentially a specialized dependent type system that tracks ownership. Ada/SPARK has similar contract-based verification. DTT brings these ideas to C++ without requiring a new language.

Imagine if every `std::vector` access was automatically bounds-checked at compile time. Not with runtime overhead — with static verification. The class of CVEs that DTT could prevent is enormous.

## Try It Yourself

```bash
git clone <repo>
cd dtt_plugin_package
./build.sh

# Compile your code with DTT
clang++ -fplugin=./DTTPlugin.so your_file.cpp
```

Replace `std::vector` with `dtt::vector`, and you get compile-time bounds checking for free. Add annotations to your own types to extend the checking.

74 tests pass. The plugin is ~2200 lines of C++. It's not production-ready, but it works.

## The Dream

The dream is a world where buffer overflows are compile errors, not CVEs. Where "just check the bounds" isn't advice you give in code review — it's something the compiler enforces. Where memory safety doesn't require rewriting everything in Rust.

DTT is a step toward that world. It's not the final answer, but it shows what's possible when you bring dependent type ideas to mainstream languages.

---

*DTT is open source. Contributions welcome.*

*Built with Clang, LLVM, and Z3.*
