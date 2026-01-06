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

## Beyond Bounds Checking: Domain-Specific Safety

DTT isn't limited to array bounds. The same mechanism can enforce arbitrary domain invariants. Here are examples that go far beyond `v[i] < v.size()`:

### Dimensional Analysis — Meters Can't Add to Seconds

```cpp
struct Quantity {
    double _value;
    int _dim_m = 0;   // meters exponent
    int _dim_s = 0;   // seconds exponent

    [[clang::annotate("dtt:ensures=_dim_m == 1"),
      clang::annotate("dtt:ensures=_dim_s == 0")]]
    static Quantity parse_length(const std::string& s);

    [[clang::annotate("dtt:ensures=_dim_m == 0"),
      clang::annotate("dtt:ensures=_dim_s == 1")]]
    static Quantity parse_time(const std::string& s);

    // Addition: dimensions must match
    [[clang::annotate("dtt:requires=_dim_m == arg0._dim_m"),
      clang::annotate("dtt:requires=_dim_s == arg0._dim_s")]]
    Quantity& operator+=(const Quantity& other);

    // Division: dimensions subtract
    [[clang::annotate("dtt:ensures=result._dim_m == arg0._dim_m - arg1._dim_m"),
      clang::annotate("dtt:ensures=result._dim_s == arg0._dim_s - arg1._dim_s")]]
    friend Quantity operator/(const Quantity& a, const Quantity& b);
};

[[clang::annotate("dtt:requires=arg0._dim_m == 1"),
  clang::annotate("dtt:requires=arg0._dim_s == -1")]]
void set_speed(const Quantity& vel);
```

```cpp
auto dist = Quantity::parse_length("2.5 cm");  // _dim_m=1, _dim_s=0
auto time = Quantity::parse_time("10 s");      // _dim_m=0, _dim_s=1

auto speed = dist / time;    // ✓ Compile-time: _dim_m=1, _dim_s=-1

dist += time;                // ✗ Error: dimensions don't match
set_speed(speed);            // ✓ OK: velocity has correct dimensions
set_speed(dist);             // ✗ Error: length is not velocity
```

The string parsing happens at runtime. The dimension arithmetic is proven at compile time.

### Cryptographic State — Can't Decrypt With Wrong Key

```cpp
struct SecureData {
    bool _encrypted = false;
    size_t _key_id = 0;

    [[clang::annotate("dtt:requires=_encrypted == 0"),
      clang::annotate("dtt:ensures=_encrypted == 1"),
      clang::annotate("dtt:ensures=_key_id == arg1")]]
    void encrypt(Key& key, size_t key_id);

    [[clang::annotate("dtt:requires=_encrypted == 1"),
      clang::annotate("dtt:requires=_key_id == arg1"),
      clang::annotate("dtt:ensures=_encrypted == 0")]]
    void decrypt(Key& key, size_t key_id);
};

[[clang::annotate("dtt:requires=arg0._encrypted == 1")]]
void send_secure(SecureData& data);
```

```cpp
SecureData secret;
send_secure(secret);         // ✗ Error: not encrypted

secret.encrypt(key, 1);
send_secure(secret);         // ✓ OK

secret.decrypt(other_key, 2); // ✗ Error: key_id(1) != 2
secret.decrypt(key, 1);      // ✓ OK: correct key
```

### Protocol State Machines — Can't Send Before Connect

```cpp
struct Channel {
    int _state = 0;  // 0=init, 1=request_sent, 2=response_received

    [[clang::annotate("dtt:requires=_state == 0"),
      clang::annotate("dtt:ensures=_state == 1")]]
    void send_request(Request& req);

    [[clang::annotate("dtt:requires=_state == 1"),
      clang::annotate("dtt:ensures=_state == 2")]]
    void receive_response(Response& resp);

    [[clang::annotate("dtt:requires=_state == 2")]]
    void close();
};
```

```cpp
Channel ch;
ch.receive_response(resp);   // ✗ Error: state is 0, not 1
ch.send_request(req);
ch.send_request(req);        // ✗ Error: state is now 1, not 0
ch.receive_response(resp);
ch.close();                  // ✓ OK
```

### Capability Security — Can't Forge Access Tokens

```cpp
struct Capability {
    size_t _resource_id = 0;
    bool _can_read = false;
    bool _can_write = false;
};

struct Resource {
    size_t _id;

    [[clang::annotate("dtt:requires=arg0._resource_id == _id"),
      clang::annotate("dtt:requires=arg0._can_write == 1")]]
    void write(Capability& cap, const std::string& data);
};
```

```cpp
Resource r{._id = 42};
Capability forged{._resource_id = 99, ._can_write = true};

r.write(forged, "hack");     // ✗ Error: resource_id(99) != 42
```

### Matrix Dimensions — Can Only Multiply Compatible Shapes

```cpp
template<typename T>
struct Matrix {
    size_t _rows = 0;
    size_t _cols = 0;

    [[clang::annotate("dtt:ensures=_rows == arg0"),
      clang::annotate("dtt:ensures=_cols == arg1")]]
    Matrix(size_t rows, size_t cols);

    // C = A * B only valid when A.cols == B.rows
    [[clang::annotate("dtt:requires=arg0._cols == arg1._rows"),
      clang::annotate("dtt:ensures=result._rows == arg0._rows"),
      clang::annotate("dtt:ensures=result._cols == arg1._cols")]]
    friend Matrix multiply(const Matrix& a, const Matrix& b);

    [[clang::annotate("dtt:requires=arg0 < _rows"),
      clang::annotate("dtt:requires=arg1 < _cols")]]
    T& at(size_t row, size_t col);
};
```

```cpp
Matrix<float> A(3, 4);  // 3x4
Matrix<float> B(4, 5);  // 4x5
Matrix<float> C(2, 4);  // 2x4

auto D = multiply(A, B);  // ✓ OK: A.cols(4) == B.rows(4) → 3x5
auto E = multiply(A, C);  // ✗ Error: A.cols(4) != C.rows(2)

D.at(2, 4);  // ✓ OK: 2 < 3, 4 < 5
D.at(3, 0);  // ✗ Error: 3 >= _rows(3)
```

### Red-Black Tree Invariants — Structural Balance

```cpp
template<typename T>
struct RBNode {
    int _color = 0;        // 0=red, 1=black
    int _black_height = 0; // Black nodes to any leaf
    size_t _size = 0;

    [[clang::annotate("dtt:ensures=_black_height >= old(_black_height)"),
      clang::annotate("dtt:ensures=_black_height <= old(_black_height) + 1"),
      clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void insert(T val);

    [[clang::annotate("dtt:requires=_color == 1")]]
    void validate_root();

    [[clang::annotate("dtt:requires=_black_height == arg0._black_height"),
      clang::annotate("dtt:ensures=_size == old(_size) + arg0._size")]]
    void merge(RBNode& other);
};
```

```cpp
RBNode<int> tree1, tree2;
tree1._black_height = 3;
tree2._black_height = 5;

tree1.merge(tree2);  // ✗ Error: _black_height(3) != arg0._black_height(5)

tree2._black_height = 3;
tree1.merge(tree2);  // ✓ OK: heights match
```

### Information Flow — Secret Data Can't Leak

```cpp
template<typename T>
struct Labeled {
    T _data;
    int _secrecy = 0;   // 0=public, 1=internal, 2=secret, 3=top_secret
    int _integrity = 0; // 0=untrusted, 1=trusted, 2=verified

    [[clang::annotate("dtt:requires=arg0._secrecy >= _secrecy")]]
    void copy_to(Labeled& dest);

    [[clang::annotate("dtt:requires=_integrity >= 2"),
      clang::annotate("dtt:ensures=_secrecy == arg0")]]
    void declassify(int new_level);
};

[[clang::annotate("dtt:requires=arg0._secrecy == 0")]]
void log_public(Labeled<std::string>& data);

[[clang::annotate("dtt:requires=arg0._secrecy <= 1"),
  clang::annotate("dtt:requires=arg0._integrity >= 1")]]
void send_external(Labeled<std::string>& data);
```

```cpp
Labeled<std::string> secret{._data="password", ._secrecy=3};
Labeled<std::string> public_log{._secrecy=0};

secret.copy_to(public_log);  // ✗ Error: 0 < 3 (leaking secret!)
log_public(secret);          // ✗ Error: _secrecy(3) != 0

secret._integrity = 2;
secret.declassify(0);
log_public(secret);          // ✓ OK: explicitly declassified
```

### Parser State — What's Consumed vs Remaining

```cpp
struct ParseState {
    size_t _pos = 0;
    size_t _len = 0;
    size_t _remaining = 0;  // _len - _pos

    [[clang::annotate("dtt:ensures=_pos == 0"),
      clang::annotate("dtt:ensures=_len == arg0"),
      clang::annotate("dtt:ensures=_remaining == arg0")]]
    ParseState(size_t input_len);

    [[clang::annotate("dtt:requires=arg0 <= _remaining"),
      clang::annotate("dtt:ensures=_pos == old(_pos) + arg0"),
      clang::annotate("dtt:ensures=_remaining == old(_remaining) - arg0")]]
    void consume(size_t n);

    [[clang::annotate("dtt:requires=arg0 <= _remaining"),
      clang::annotate("dtt:ensures=_pos == old(_pos)")]]
    char peek(size_t offset);

    [[clang::annotate("dtt:requires=_remaining == 0")]]
    void expect_eof();
};
```

```cpp
ParseState p(100);  // 100 char input

p.expect_eof();     // ✗ Error: _remaining(100) != 0
p.consume(50);      // _remaining = 50
p.consume(60);      // ✗ Error: 60 > _remaining(50)
p.peek(49);         // ✓ OK

p.consume(50);      // _remaining = 0
p.expect_eof();     // ✓ OK
```

### What Makes These Different

| Example | Key Insight |
|---------|-------------|
| Units | Compile-time arithmetic on runtime-parsed values |
| Crypto | Multi-property transformation chains, key binding |
| Protocols | State machine enforcement, ordering constraints |
| Capabilities | Resource binding, unforgeable tokens |
| Matrix | Cross-argument dimension constraints |
| Red-black tree | Structural/balance invariants |
| Information flow | Security lattice, non-interference |
| Parser | Conservation laws, resource accounting |

**Common theme:** These go beyond simple state checks to express **relationships**, **transformations**, and **invariants** that span multiple values or encode domain-specific laws.

The magic: **runtime flexibility of dynamic values + compile-time safety of static verification.**

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
