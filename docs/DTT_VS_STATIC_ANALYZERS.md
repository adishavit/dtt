# DTT vs Other Static Analyzers: A Deep Comparison

## The Fundamental Difference

**DTT finds bugs. Other analyzers find code that *might* have bugs.**

This is the key philosophical distinction. When Clang Static Analyzer (CSA) or Coverity reports an issue, it's saying "I couldn't prove this is safe" or "this pattern often indicates a bug." When DTT reports an error, it's saying "I proved this code *will* fail."

```cpp
std::vector<int> v;
v.push_back(1);
v.pop_back();
v.front();  // DTT: ERROR - v._size = 0, front() requires _size > 0
            // CSA: (might warn, might not - depends on path sensitivity)
```

---

## Analysis Techniques

### Clang Static Analyzer (CSA)

**Technique:** Symbolic execution on an "exploded graph" (path-sensitive analysis)

CSA explores execution paths, maintaining symbolic state at each program point. It tracks memory regions and symbolic values through the program's control flow graph.

```
             [Entry]
                |
            [v.empty()]
               /  \
      [true]       [false]
         |            |
    [return]      [v.front()]  <- CSA reaches here only on one path
```

**Strengths:**
- Path-sensitive (knows which branch was taken)
- Memory region tracking (heap, stack, globals)
- Good for use-after-free, memory leaks

**Weaknesses:**
- Path explosion limits depth
- No semantic understanding of containers
- Can't track user-defined properties
- No SMT prover integration

### DTT

**Technique:** Property tracking + contract verification + Z3 SMT solving

DTT doesn't simulate execution. It tracks *semantic properties* through the code and verifies contracts algebraically.

```cpp
std::vector<int> v;     // DTT: v._size = 0
v.push_back(1);         // DTT: v._size = 1  (postcondition: _size = old(_size) + 1)
v.push_back(2);         // DTT: v._size = 2
v[1];                   // Check: 1 < _size?  1 < 2 OK
v[2];                   // Check: 2 < _size?  2 < 2 ERROR
```

**Strengths:**
- Semantic understanding (knows what `push_back` *means*)
- User-definable properties via annotations
- Z3 SMT for complex arithmetic
- Auto-inference of postconditions
- Guards work: `if (!v.empty())` proves `v._size > 0`

**Weaknesses:**
- Requires annotations for non-STL types
- No path sensitivity beyond guards
- Limited alias tracking

---

## What Each Tool Actually Tracks

| Property | DTT | CSA | clang-tidy | Coverity | Infer |
|----------|-----|-----|------------|----------|-------|
| Container size | Symbolic | No | No | No | Partial |
| Null pointers | Yes | Yes | Partial | Yes | Yes |
| Use-after-free | Yes | Yes | No | Yes | Yes |
| Iterator invalidation | Yes | Partial | Partial | Yes | Partial |
| Optional has_value | Yes | No | No | No | No |
| Variant index | Yes | No | No | No | No |
| State machines | Yes | No | No | Partial | No |
| User-defined props | Yes | No | No | No | No |
| Complex arithmetic | Yes (Z3) | Limited | No | Limited | Limited |

### Example: The Empty Container Bug

```cpp
void process(std::vector<int>& v) {
    if (v.size() > 0) {
        v.pop_back();
    }
    v.front();  // BUG: v might be empty now!
}
```

**DTT:**
```
error: DTT: vector::front requires _size > 0
       At call site: v._size is UNKNOWN (was > 0, then decremented)
```

**CSA:** Unlikely to catch this. CSA tracks memory regions but doesn't understand that `pop_back()` semantically decrements the size.

**clang-tidy:** No check for this pattern.

**Coverity:** Might catch with aggressive checkers, but not guaranteed.

---

## The Contract Model

DTT's killer feature is its contract system:

```cpp
struct Buffer {
    [[clang::annotate("dtt:requires=_size > 0")]]
    void pop();

    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push(int x);

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](size_t i);
};
```

**No other static analyzer can do this.** You cannot teach CSA that "pop requires size > 0." You'd have to write a custom checker in C++.

### Auto-Inference: No Annotations Needed

DTT can infer postconditions from implementations:

```cpp
void Vec::resize(int n) {
    _size = n;  // DTT infers: ensures=_size == arg0
}
```

---

## The Guard System

DTT understands that runtime checks create compile-time proofs:

```cpp
std::vector<int> v = get_data();  // _size = UNKNOWN

v.front();  // ERROR: can't prove _size > 0

if (!v.empty()) {
    v.front();  // OK: empty() returning false proves _size > 0
}

if (v.size() >= 5) {
    v[4];  // OK: size >= 5 proves 4 < _size
}
```

**CSA** can do similar reasoning within a single path, but it doesn't have semantic knowledge of what `empty()` *means* for `_size`.

---

## Z3 Integration: Proving Complex Constraints

```cpp
void copy_range(Buffer& b, int start, int len) {
    // DTT annotation: requires=start + len <= _size
}

Buffer buf(100);  // _size = 100
copy_range(buf, 30, 70);   // 30 + 70 = 100 <= 100 OK
copy_range(buf, 50, 60);   // 50 + 60 = 110 > 100 ERROR
```

DTT sends this to Z3:
```smt
(declare-const _size Int)
(declare-const start Int)
(declare-const len Int)
(assert (= _size 100))
(assert (= start 50))
(assert (= len 60))
(assert (not (<= (+ start len) _size)))  ; negated constraint
(check-sat)  ; SAT -> constraint violated
```

**No other C++ static analyzer has SMT integration for arbitrary user constraints.**

---

## Error Categorization

DTT has a sophisticated error model:

| Category | Meaning | Default Behavior |
|----------|---------|------------------|
| **E: Known Safe** | Proven correct | Silent |
| **F: Proven Bug** | Proven violation | **Error** |
| **G: Annotation Error** | Malformed contract | **Error** |
| **H: Memory Safety** | UAF, double-free, uninitialized, leak | **Error** |
| **C: Lost Tracking** | Unknown call broke tracking | Warning |
| **D: Solver Undecidable** | Z3 timed out | Warning |

This means DTT errors (F, G, H) are **always real bugs**. Categories C and D are uncertainties, shown as warnings.

**CSA** doesn't distinguish—all reports are "potential bugs."

---

## Comparison Table

| Aspect | DTT | CSA | clang-tidy | Coverity | Infer |
|--------|-----|-----|------------|----------|-------|
| **Paradigm** | Contract verification | Symbolic execution | Pattern matching | Dataflow + patterns | Separation logic |
| **False positives** | Near zero (for errors) | Low | Medium-High | Low | Low |
| **False negatives** | Unannotated code | Path explosion | Many | Some | Some |
| **Extensibility** | Annotations | C++ checker | C++ check | Proprietary | OCaml |
| **Learning curve** | Low (annotations) | High (checkers) | Medium | Low (use) | Medium |
| **SMT prover** | Z3 | No | No | No | No |
| **Runtime cost** | Zero | Zero | Zero | Zero | Zero |
| **Semantic model** | User-defined | Built-in memory | None | Built-in patterns | Memory |
| **CI integration** | Plugin flag | Built-in | Built-in | Separate | Separate |

---

## What Each Tool Catches

| Bug Type | DTT | Sanitizers | Analyzers | Contracts | Concepts |
|----------|-----|------------|-----------|-----------|----------|
| Array OOB | Proven | Detected | Sometimes | Runtime | No |
| Null deref | Annotated | Yes | Yes | Runtime | No |
| Use-after-free | Partial | Yes | Sometimes | No | No |
| Empty container access | Proven | Detected | Sometimes | Runtime | No |
| Invalid state transition | Proven | No | No | Runtime | No |
| Dimension mismatch | Proven | No | No | Runtime | Yes (templates) |
| Protocol violation | Proven | No | No | Runtime | No |
| Data races | No | Yes (TSan) | Sometimes | No | No |
| Integer overflow | No | Yes (UBSan) | Sometimes | Runtime | No |
| Resource leaks | Yes | Yes (LSan) | Sometimes | No | No |

---

## What Each Tool Excels At

### CSA Strengths
- Memory leak detection
- Null pointer analysis in complex paths
- Use-after-free across function calls
- Mature, battle-tested in production

### clang-tidy Strengths
- Style enforcement (naming, formatting)
- Modernization (C++11/14/17/20 upgrades)
- Simple bug patterns
- Quick integration

### Coverity Strengths
- Industrial-scale codebase analysis
- Low false-positive rate
- Security-focused checkers
- Track record in enterprise

### Infer Strengths
- Interprocedural analysis
- Memory safety via separation logic
- Race condition detection
- Scales to large codebases

### DTT Strengths
- **User-defined semantic properties**
- **Bounds checking with symbolic sizes**
- **State machine enforcement**
- **Value concepts (sorted, forall)**
- **Ghost state for unforgeable properties**
- **Assignment and move tracking**
- **Auto postcondition inference**
- **Resource leak detection (FILE*, malloc, fd)**

---

## When to Use What

| Scenario | Best Tool |
|----------|-----------|
| **"Prove my buffer accesses are safe"** | DTT |
| **"Find memory leaks"** | CSA, Infer, Coverity |
| **"Modernize to C++17"** | clang-tidy |
| **"Pre-commit quick scan"** | clang-tidy |
| **"Track custom type invariants"** | DTT |
| **"Enterprise audit trail"** | Coverity |
| **"State machine verification"** | DTT |
| **"Find data races"** | Infer, ThreadSanitizer |

---

## Refinement Type Systems (DTT's True Peers)

DTT isn't the first tool to bring dependent types to mainstream languages. Here's how it compares to established verification systems:

| Aspect | DTT | Liquid Haskell | Frama-C | SPARK Ada | Dafny |
|--------|-----|----------------|---------|-----------|-------|
| **Host language** | C++ | Haskell | C | Ada | Dafny |
| **Approach** | Annotations | Refinement types | ACSL comments | Aspects | Native |
| **Prover** | Z3 | Z3 | Why3 | Various | Z3 |
| **Inference** | Partial | Yes | Limited | Limited | Yes |
| **Quantifiers** | No | Yes | Yes | Yes | Yes |
| **Loop invariants** | Partial | Yes | Yes | Yes | Yes |
| **Termination proofs** | No | Yes | Yes | Yes | Yes |
| **Learning curve** | Low | Medium | High | Medium | Low |
| **Production use** | Experimental | Yes | Yes (aerospace) | Yes (aerospace) | Limited |

DTT is deliberately less powerful than these systems. It trades full verification for accessibility—you can start using it in 5 minutes without learning a new language or proof system.

---

## The Verification Spectrum

```
    Proof Strength
          ^
          |
    Idris + Full dependent types (rewrite your code)
          |
   Frama-C+ Industrial verification (ACSL contracts)
    SPARK |
          |
      DTT + Lightweight contracts (annotations)
          |
Contracts + Runtime assertions (C++26)
          |
      CSA + Symbolic execution (path-limited)
          |
clang-tidy+ Pattern matching (syntax-based)
          |
          +-------------------------------------->
                     Adoption Friction
```

DTT occupies a sweet spot: stronger guarantees than runtime tools, but lower friction than full verification systems.

---

## Unique DTT Capabilities

### Ghost State (`@` prefix)

Properties that exist only in the compiler's symbolic tracking, never as runtime fields:

```cpp
struct SecureData {
    std::vector<char> payload;  // Real data only

    [[clang::annotate("dtt:requires=@encrypted == 0"),
      clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt(Key& key);
};

SecureData s;
s.@encrypted = true;    // SYNTAX ERROR: @ is invalid in C++ identifiers
```

No other analyzer has unforgeable ghost state.

### Deferred Guard Evaluation

Resource tracking with deferred postcondition application:

```cpp
FILE* f = fopen("data.txt", "r");  // f.@open = PENDING when f._nonnull != 0
if (f) {
    // Guard proves f._nonnull != 0 -> applies deferred postcondition
    // Now: f.@open = FileIO.OPEN
    fclose(f);  // f.@open = FileIO.CLOSED
}
// No leak: f.@open was never OPEN on the null path
```

### Transitive Precondition Propagation

```cpp
void inner(std::vector<int>& v) {
    v.front();  // requires _size > 0
}

void outer(std::vector<int>& v) {
    inner(v);  // DTT infers: outer also requires _size > 0
}

std::vector<int> empty;
outer(empty);  // ERROR: _size = 0, but outer requires _size > 0
```

---

## Conclusion

**Use DTT when:**
- You need provable bounds safety
- You want to track custom semantic properties
- You're building state machines or protocols
- You want compile-time guarantees, not runtime checks

**Use CSA when:**
- You need memory leak detection
- You're analyzing legacy code without annotations
- You need path-sensitive null analysis

**Use clang-tidy when:**
- You want style enforcement
- You're modernizing code
- You need quick CI integration

**Use Coverity/Infer when:**
- You have enterprise requirements
- You need cross-function analysis at scale
- You're focused on security vulnerabilities

The tools aren't competitors—they're complementary. DTT proves contracts. CSA finds memory bugs. clang-tidy enforces style. Use all of them.

---

*DTT: Because "just check the bounds" should be enforced, not advised.*
