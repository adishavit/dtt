# Catching Buffer Overflows at Compile Time: Introducing DTT

**TL;DR:** DTT is a Clang plugin that brings Idris-style dependent type checking to C++, catching out-of-bounds access, empty container errors, and use-after-move bugs at compile time — before your code ever runs.

---

## The Problem: Memory Safety is Still Eating Us Alive

We're still writing CVEs about buffer overflows. Microsoft reports that 70% of their security vulnerabilities are memory safety issues. Google says the same about Chrome. The CWE Top 25 is dominated by CWE-787 (Out-of-bounds Write) and CWE-125 (Out-of-bounds Read).

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
void example() {
    std::vector<int> v;      // DTT knows: _size = 0

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

DTT is a pure static analysis tool. The annotations are only read by the plugin during compilation — they generate no runtime code. Your `std::vector<T>` compiles down to exactly the same machine code whether DTT is enabled or not. No bounds-checking branches. No debug assertions. No performance penalty.

This is fundamentally different from runtime tools like AddressSanitizer or debug iterators. Those add overhead (sometimes 2-10x slowdown) and are typically disabled in production. DTT's checks happen entirely at compile time — if your code compiles, the checks have passed, and the resulting binary runs at full speed.

You get the safety of bounds checking with the performance of unchecked code.

## But What About Runtime Values?

Here's where it gets interesting. DTT handles runtime values through *guards*:

```cpp
void process(std::vector<int>& v, int i) {
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
void fill(std::vector<int>& v) {
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

## Value Concepts: Beyond Single Elements

What about properties over entire collections? "All elements are positive." "The array is sorted." "Every pointer is valid."

DTT supports bounded quantifiers for these collection-wide properties:

```cpp
// Require all elements positive
[[clang::annotate("dtt:requires=forall(i, 0, n, arr[i] > 0)")]]
double geometric_mean(const int* arr, size_t n) {
    double product = 1.0;
    for (size_t i = 0; i < n; i++)
        product *= arr[i];  // Safe: arr[i] > 0 proven
    return pow(product, 1.0 / n);
}

void caller() {
    int good[] = {1, 2, 3, 4, 5};
    geometric_mean(good, 5);   // ✓ OK: all positive

    int bad[] = {1, -2, 3};
    geometric_mean(bad, 3);    // ✗ ERROR: bad[1] = -2 not > 0
}
```

Even better: predefined concepts with automatic inference:

```cpp
[[clang::annotate("dtt:requires=sorted")]]
int binary_search(const std::vector<int>& v, int target);

void caller() {
    std::vector<int> data = {3, 1, 4, 1, 5};

    binary_search(data, 3);    // ✗ ERROR: not proven sorted

    std::sort(data.begin(), data.end());  // AUTO: establishes sorted

    binary_search(data, 3);    // ✓ OK: sorted proven
}
```

This catches Heartbleed-class bugs where the issue isn't a single index, but a mismatch between claimed and actual buffer contents.

## Assignment and Move Tracking

DTT tracks properties through assignment and swap operations:

```cpp
std::vector<int> v1, v2;
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
| Wrong variant type | `_variant_index` tracking, `holds_alternative` guards |
| Unsorted binary search | `requires=sorted` with auto-inference from `std::sort` |
| Invalid element values | `forall(i, 0, n, arr[i] > 0)` quantifiers |
| Wrong any_cast type | `_any_type` tracking, `a.type() == typeid(T)` guards |

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

#### The Problem: Tracking Fields Can Be Hacked

But wait — there's a flaw. Since `_encrypted` is a real runtime field, malicious or buggy code can bypass the state machine:

```cpp
SecureData secret;
secret._encrypted = true;    // Compiles! Bypasses encrypt()
secret._key_id = 42;         // Forge any key ID
send_secure(secret);         // DTT thinks it's encrypted — but it's not!
```

The tracking fields are just regular struct members. Nothing stops code from writing to them directly.

#### The Solution: Ghost State with `@` Prefix

DTT supports **ghost state** — properties that exist only in the compiler's symbolic tracking, never as runtime fields. Use the `@` prefix:

```cpp
struct SecureData {
    std::vector<char> payload;  // Real data only — no tracking fields!

    [[clang::annotate("dtt:requires=@encrypted == 0"),
      clang::annotate("dtt:ensures=@encrypted == 1"),
      clang::annotate("dtt:ensures=@key_id == arg1")]]
    void encrypt(Key& key, size_t key_id);

    [[clang::annotate("dtt:requires=@encrypted == 1"),
      clang::annotate("dtt:requires=@key_id == arg1"),
      clang::annotate("dtt:ensures=@encrypted == 0")]]
    void decrypt(Key& key, size_t key_id);
};

[[clang::annotate("dtt:requires=arg0.@encrypted == 1")]]
void send_secure(SecureData& data);
```

Now the hack attempt fails at the syntax level:

```cpp
SecureData secret;
secret.@encrypted = true;    // SYNTAX ERROR: @ is invalid in C++ identifiers
```

The `@` character is illegal in C++ identifiers, so `@encrypted` can never be a real field. It exists purely as compile-time verification metadata — unforgeable, zero runtime overhead, impossible to tamper with.

**When to use which:**
- `_size`, `_capacity` — Properties that mirror actual runtime state
- `@encrypted`, `@authenticated`, `@validated` — Pure verification state that should be unforgeable

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

## C Library Tracking — No Wrappers Required

Here's something unexpected: DTT can track **plain C library types** without any wrapper classes.

```cpp
#include <cstdio>
char buf[100];

void demo() {
    FILE* f = fopen("data.txt", "r");

    fread(buf, 1, 10, f);       // ✗ Warning: f may be null

    if (f) {
        fwrite("hi", 1, 2, f);  // ✗ Error: opened read-only
        fclose(f);
        fclose(f);              // ✗ Error: double close
    }
}
```

DTT tracks three properties on `FILE*`:
- **`_nonnull`** — Did you check the return value of `fopen`?
- **`_readable`** — Was it opened with `"r"`, `"r+"`, `"w+"`, or `"a+"`?
- **`_writable`** — Was it opened with `"w"`, `"a"`, `"r+"`, `"w+"`, or `"a+"`?

This works because DTT has built-in annotations for C library functions. When you call `fopen("file.txt", "r")`, DTT:
1. Sets `_nonnull = unknown` (fopen can return NULL)
2. Parses the mode string and sets `_readable = 1`, `_writable = 0`

When you call `fclose(f)`, DTT:
1. Checks `_nonnull != 0` (can't close a null pointer)
2. Sets `_nonnull = 0` (pointer is now invalid)

The third call to `fclose` fails because `_nonnull` is already 0.

**No wrappers. No templates. Just `FILE*`.** The same C code you'd write anyway — but now with compile-time safety.

### Use-After-Free Detection for String/Memory Functions

DTT also tracks pointer lifetime through C library calls. If you pass a freed pointer to `strcpy`, `memcpy`, `strlen`, or similar functions, DTT catches it:

```cpp
void dangerous() {
    char* buf = (char*)malloc(100);
    if (!buf) return;
    strcpy(buf, "Hello");
    free(buf);

    strcpy(buf, "World");     // ✗ ERROR: use-after-free of 'buf'
    strlen(buf);              // ✗ ERROR: use-after-free of 'buf'
    memcpy(dst, buf, 10);     // ✗ ERROR: use-after-free of 'buf'
}
```

This works for all common C string and memory functions:

| Function | Checked Args |
|----------|--------------|
| `strcpy`, `strncpy`, `strcat`, `strncat` | dst, src |
| `strlen`, `strchr`, `strrchr` | string |
| `strcmp`, `strncmp`, `strstr` | both strings |
| `memcpy`, `memmove`, `memcmp` | dst, src |
| `memset`, `memchr` | buffer |
| `sprintf`, `snprintf`, `sscanf` | buffer |

The detection works through DTT's lifetime tracking — when you call `free(p)`, DTT marks `p` as freed. Any subsequent use of `p` as an argument to these functions triggers an error.

## What Idris Does — And What DTT Can Do Today

Idris, the poster child for dependent types, tracks properties like these:

| Category | Idris Example | DTT Today? |
|----------|---------------|------------|
| Lengths | `Vect n a` | ✓ `_size` |
| Non-empty | `List1 a` | ✓ `_size > 0` |
| Bounds | `Fin n` | ✓ `arg0 < _size` |
| State machines | `Door Open` | ✓ |
| Resources | `File Read` | ✓ |
| Nullability | `Maybe a` | ✓ |
| Sorted | `Sorted xs` | ✓ |
| Format validation | `Email` | ✓ |

The surprise: **most of these work in DTT today** — no plugin changes needed. DTT tracks integer and boolean properties generically. Here's how.

### Connection State Machine

```cpp
struct Connection {
    int _state = 0;  // 0=closed, 1=open, 2=authenticated

    [[clang::annotate("dtt:requires=_state == 0"),
      clang::annotate("dtt:ensures=_state == 1")]]
    void connect();

    [[clang::annotate("dtt:requires=_state == 1"),
      clang::annotate("dtt:ensures=_state == 2")]]
    void authenticate();

    [[clang::annotate("dtt:requires=_state == 2")]]
    void send(const char* data);
};
```

```cpp
Connection c;
c.send("hello");      // ✗ Error: requires _state == 2
c.connect();
c.send("hello");      // ✗ Error: requires _state == 2
c.authenticate();
c.send("hello");      // ✓ OK
```

### File Resources

```cpp
struct File {
    bool _open = false;
    bool _readable = false;
    bool _writable = false;

    [[clang::annotate("dtt:ensures=_open == 1"),
      clang::annotate("dtt:ensures=_readable == 1"),
      clang::annotate("dtt:ensures=_writable == 0")]]
    static File open_read(const char* path);

    [[clang::annotate("dtt:requires=_open == 1"),
      clang::annotate("dtt:requires=_readable == 1")]]
    std::string read();

    [[clang::annotate("dtt:requires=_open == 1"),
      clang::annotate("dtt:requires=_writable == 1")]]
    void write(const std::string& data);

    [[clang::annotate("dtt:requires=_open == 1"),
      clang::annotate("dtt:ensures=_open == 0")]]
    void close();
};
```

```cpp
auto f = File::open_read("data.txt");
f.write("test");  // ✗ Error: requires _writable == 1
f.read();         // ✓ OK
f.close();
f.read();         // ✗ Error: requires _open == 1
```

### Optional Values

```cpp
template<typename T>
struct Optional {
    bool _has_value = false;

    [[clang::annotate("dtt:ensures=_has_value == 1")]]
    Optional(T val);

    [[clang::annotate("dtt:ensures=_has_value == 0")]]
    Optional();

    [[clang::annotate("dtt:requires=_has_value == 1")]]
    T& value();

    [[clang::annotate("dtt:ensures=_has_value == 0")]]
    void reset();
};
```

```cpp
Optional<int> opt;
opt.value();      // ✗ Error: requires _has_value == 1

opt = Optional<int>(42);
opt.value();      // ✓ OK

opt.reset();
opt.value();      // ✗ Error: requires _has_value == 1
```

### Type-Safe Variant and Any

DTT tracks which alternative is active in `std::variant` and the contained type in `std::any`:

```cpp
std::variant<int, std::string> v = 42;  // DTT knows: _variant_index = 0
std::get<int>(v);                        // ✓ OK: index 0 is int
std::get<std::string>(v);                // ✗ Error: _variant_index != 1

if (std::holds_alternative<std::string>(v)) {
    std::get<std::string>(v);            // ✓ OK: guard proves string
} else {
    std::get<std::string>(v);            // ✗ Error: proved NOT string
}
```

```cpp
std::any a = 42;                         // DTT knows: _any_type = int
std::any_cast<int>(a);                   // ✓ OK: type matches
std::any_cast<double>(a);                // ✗ Error: _any_type != double

if (a.type() == typeid(int)) {
    std::any_cast<int>(a);               // ✓ OK: guard proves int
} else {
    std::any_cast<int>(a);               // ✗ Error: proved NOT int
}
```

This catches `std::bad_variant_access` and `std::bad_any_cast` at compile time.

### Sorted Containers

```cpp
template<typename T>
struct SortedVec {
    size_t _size = 0;
    bool _sorted = true;

    [[clang::annotate("dtt:ensures=_sorted == 1"),
      clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void insert_sorted(T val);  // Maintains order

    [[clang::annotate("dtt:ensures=_sorted == 0"),
      clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push_back(T val);  // Breaks order

    [[clang::annotate("dtt:requires=_sorted == 1")]]
    bool binary_search(T val);

    [[clang::annotate("dtt:ensures=_sorted == 1")]]
    void sort();
};
```

```cpp
SortedVec<int> v;
v.push_back(3);
v.push_back(1);
v.binary_search(2);  // ✗ Error: requires _sorted == 1

v.sort();
v.binary_search(2);  // ✓ OK
```

### Input Validation

```cpp
struct UserInput {
    bool _validated = false;
    bool _is_email = false;
    bool _sql_escaped = false;

    [[clang::annotate("dtt:ensures=_validated == 1"),
      clang::annotate("dtt:ensures=_is_email == 1")]]
    void validate_email();

    [[clang::annotate("dtt:ensures=_sql_escaped == 1")]]
    void escape_sql();

    [[clang::annotate("dtt:requires=_is_email == 1")]]
    void send_to(const char* smtp_server);

    [[clang::annotate("dtt:requires=_sql_escaped == 1")]]
    std::string as_sql_param();
};
```

```cpp
UserInput input{user_string};
input.send_to("mail.server.com");  // ✗ Error: requires _is_email == 1

input.validate_email();
input.send_to("mail.server.com");  // ✓ OK

query("SELECT * WHERE email = " + input.as_sql_param());
// ✗ Error: requires _sql_escaped == 1
```

### The Pattern

All these examples share the same structure: **encode properties as booleans or integers, express contracts as equality constraints**.

Idris encodes these in types. DTT encodes them in annotations. Same expressive power — different syntax.

## What DTT Doesn't Catch

Let's be honest about limitations:

- **Runtime-only violations**: If the bug only manifests with specific runtime input that DTT can't reason about, it won't catch it
- **Logic errors**: DTT verifies contracts, not correctness
- **Unannotated code**: Only works with annotated types and STL containers with built-in tracking
- **C code**: This is a C++ Clang plugin
- **Complex aliasing**: `v1` and `v2` pointing to the same vector will confuse tracking

## The Bigger Picture

DTT is a proof of concept, but the approach is sound. Rust's borrow checker is essentially a specialized dependent type system that tracks ownership. Ada/SPARK has similar contract-based verification. DTT brings these ideas to C++ without requiring a new language.

Imagine if every `std::vector` access was automatically bounds-checked at compile time. Not with runtime overhead — with static verification. The class of CVEs that DTT could prevent is enormous.

## How DTT Compares to Other Tools

"Why not just use AddressSanitizer? Or wait for C++26 contracts? Or use a static analyzer?"

Fair questions. Each tool occupies a different point in the design space. Here's how they compare.

### C++ Tools & Mechanisms

| Aspect | DTT | Sanitizers | Static Analyzers | Contracts (C++26) | Concepts (C++20) |
|--------|-----|------------|------------------|-------------------|------------------|
| **When** | Compile | Runtime | Compile | Runtime | Compile |
| **What it checks** | Value properties | Memory/behavior | Patterns/dataflow | Assertions | Type capabilities |
| **Core question** | "Is `v.size > 0`?" | "Did OOB happen?" | "Does this look wrong?" | "Did precondition hold?" | "Does T have `.size()`?" |
| **Proof** | Yes (Z3) | No | No | No | N/A |
| **False positives** | None | None | Some | None | None |
| **False negatives** | Unannotated code | Untested paths | Complex paths | None (if checked) | N/A |
| **Runtime cost** | Zero | 2-10x | Zero | Non-zero | Zero |
| **Tracks state changes** | ✓ | ✗ | Limited | ✗ | ✗ |
| **Production safe** | ✓ | ✗ | ✓ | Configurable | ✓ |

The key insight: **Sanitizers find bugs you triggered. DTT proves bugs can't exist.**

### Refinement Type Systems (DTT's True Peers)

DTT isn't the first tool to bring dependent types to mainstream languages. Here's how it compares to established verification systems:

| Aspect | DTT | Liquid Haskell | Frama-C | SPARK Ada | Dafny |
|--------|-----|----------------|---------|-----------|-------|
| **Host language** | C++ | Haskell | C | Ada | Dafny |
| **Approach** | Annotations | Refinement types | ACSL comments | Aspects | Native |
| **Prover** | Z3 | Z3 | Why3 | Various | Z3 |
| **Inference** | None | Yes | Limited | Limited | Yes |
| **Quantifiers (∀, ∃)** | ✗ | ✓ | ✓ | ✓ | ✓ |
| **Loop invariants** | Partial | ✓ | ✓ | ✓ | ✓ |
| **Termination proofs** | ✗ | ✓ | ✓ | ✓ | ✓ |
| **Learning curve** | Low | Medium | High | Medium | Low |
| **Production use** | Experimental | Yes | Yes (aerospace) | Yes (aerospace) | Limited |

DTT is deliberately less powerful than these systems. It trades full verification for accessibility — you can start using it in 5 minutes without learning a new language or proof system.

### What Each Tool Catches

| Bug Type | DTT | Sanitizers | Analyzers | Contracts | Concepts |
|----------|-----|------------|-----------|-----------|----------|
| Array OOB | ✓ Proven | ✓ Detected | Sometimes | ✓ Runtime | ✗ |
| Null deref | Annotated | ✓ | ✓ | ✓ Runtime | ✗ |
| Use-after-free | Partial | ✓ | Sometimes | ✗ | ✗ |
| Empty container access | ✓ Proven | ✓ Detected | Sometimes | ✓ Runtime | ✗ |
| Invalid state transition | ✓ Proven | ✗ | ✗ | ✓ Runtime | ✗ |
| Dimension mismatch | ✓ Proven | ✗ | ✗ | ✓ Runtime | ✓ (templates) |
| Protocol violation | ✓ Proven | ✗ | ✗ | ✓ Runtime | ✗ |
| Data races | ✗ | ✓ (TSan) | Sometimes | ✗ | ✗ |
| Integer overflow | ✗ | ✓ (UBSan) | Sometimes | ✓ Runtime | ✗ |

### When to Use What

| Situation | Best Tool |
|-----------|-----------|
| Critical code, must prove correct | **DTT**, Frama-C, SPARK |
| Legacy codebase, find bugs fast | **Sanitizers** |
| General code quality | **Static Analyzers** |
| Runtime safety net | **Contracts** |
| Generic programming | **Concepts** |
| Zero runtime overhead required | **DTT**, Concepts, Analyzers |

### The Landscape

```
                        Proof Strength
                              ▲
                              │
                    Idris ────┼──── Full dependent types
                              │
              Liquid Haskell ─┼──── Refinement types
                   Frama-C    │
                  SPARK Ada ──┼──── Industrial verification
                      Dafny   │
                              │
                        DTT ──┼──── Lightweight refinements
                              │
                  Contracts ──┼──── Runtime assertions
                              │
                 Sanitizers ──┼──── Runtime detection
                              │
                  Analyzers ──┼──── Heuristic warnings
                              │
                              └─────────────────────────────►
                                        Effort / Friction
```

DTT occupies a sweet spot: stronger guarantees than runtime tools, but lower friction than full verification systems. It's verification for the rest of us.

## Try It Yourself

```bash
git clone <repo>
cd dtt

# Build
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build

# Compile your code with DTT
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 your_file.cpp
```

Use `std::vector` as normal — DTT automatically tracks it and gives you compile-time bounds checking for free. Add annotations to your own types to extend the checking.

It's not production-ready, but it works.

## The Dream

The dream is a world where buffer overflows are compile errors, not CVEs. Where "just check the bounds" isn't advice you give in code review — it's something the compiler enforces. Where memory safety doesn't require rewriting everything in Rust.

DTT is a step toward that world. It's not the final answer, but it shows what's possible when you bring dependent type ideas to mainstream languages.

---

*DTT is open source. Contributions welcome.*

*Built with Clang, LLVM, and Z3.*
