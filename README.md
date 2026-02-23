# DTT: Dependent Type Tracking for C++

[![CI](https://github.com/adishavit/dtt/actions/workflows/ci.yml/badge.svg)](https://github.com/adishavit/dtt/actions/workflows/ci.yml)

**Catch CVE-class bugs at compile time.** Buffer overflows, null pointer dereferences, use-after-move — DTT proves your code safe or shows you exactly where it's not.

## The Bugs That Ship

This compiles. It passes code review. It ships. Then it gets a CVE number.

```cpp

void vulnerable(int* session) {
    vector<int> data(2);              // _size = 2
    data.pop_back();                  // _size = 1
    data.pop_back();                  // _size = 0
    int first = data.front();         // BUG 1: empty container

    if (rand() % 2)
        session = nullptr;
    *session = first;                 // BUG 2: null dereference

    vector<int> backup = std::move(data);
    data[0] = 999;                    // BUG 3: use after move
}
```

## Without DTT

**Compiles clean. Zero warnings. All 3 bugs ship to production.**

## With DTT

```bash

error: DTT: vector::front requires _size > 0, but data._size = 0
   10 |     int first = data.front();
      |                 ^
note: data._size set to 2 (vector constructor)
    7 |     vector<int> data(2);
      |                ^
note: data._size set to 1 (vector::pop_back)
    8 |     data.pop_back();
      |     ^
note: data._size set to 0 (vector::pop_back)
    9 |     data.pop_back();
      |     ^
note: add guard: if (!data.empty()) { ... }
note: insert guard here
   10 |     int first = data.front();
      |                 ^
      |                 if (!data.empty())

error: DTT: potential null pointer dereference of 'session' (known to be null)
   14 |     *session = first;
      |     ^

error: DTT: vector::operator[] index 0 out of bounds (requires < 0, but data._size = 0)
   17 |     data[0] = 999;
      |     ^

3 errors generated.
```

| Bug | Class | Real-World Impact |
|-----|-------|-------------------|
| **#1** | Empty container access | Heartbleed (buffer over-read), countless others |
| **#2** | Null pointer dereference | 70% of Chrome/Firefox security bugs |
| **#3** | Use after move/free | Use-after-free: top memory corruption CVE class |

**3 bugs. 3 errors. Zero runtime cost.**

## How It Works

DTT is a Clang plugin that tracks *semantic properties* through your code:

```cpp
std::vector<int> v;        // DTT knows: v._size = 0
v.push_back(1);            // DTT knows: v._size = 1
v.push_back(2);            // DTT knows: v._size = 2

v[0] = 10;                 // OK: 0 < 2 ✓
v[1] = 20;                 // OK: 1 < 2 ✓
v[2] = 30;                 // ERROR: 2 < 2 ✗
```

When you call a method with a precondition, DTT *proves* it's satisfied — or shows you the counterexample.

## Under The Hood

DTT has built-in annotations for standard library types. Internally, it tracks `std::vector` like this:

```cpp
// Conceptually (annotations are built into the plugin):
// std::vector::front() has precondition: _size > 0
// std::vector::push_back() has postcondition: _size = old(_size) + 1
// std::vector::clear() has postcondition: _size = 0
// std::vector::operator[](i) has precondition: i < _size
// ... 30+ tracked methods
```

You can annotate your own types the same way with `[[clang::annotate(...)]]`.

## Quick Start

```bash
# Ubuntu/Debian
sudo apt install clang-18 llvm-18-dev libclang-18-dev cmake libz3-dev

# Build
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build

# Try it
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 your_file.cpp
```

## Integration

### Direct Usage
```bash
clang++ -fplugin=/path/to/libDTTPlugin.so -std=c++20 your_file.cpp
```

### CMake Project
```cmake
add_compile_options(-fplugin=/path/to/libDTTPlugin.so)
```

### Makefile
```makefile
CXXFLAGS += -fplugin=/path/to/libDTTPlugin.so
```

### IDE Support (clangd)

Generate `compile_commands.json`:
```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ...
ln -s build/compile_commands.json .
```

## What DTT Catches

| Bug Class | Example | CVE Examples |
|-----------|---------|--------------|
| **Buffer overflow** | `v[v.size()]` | Heartbleed, countless others |
| **Raw pointer OOB** | `int* p = arr; p[10]` on 10-element array | Buffer overflows everywhere |
| **Empty container access** | `v.front()` on empty | Container UB bugs |
| **Null dereference** | `*ptr` when maybe-null | Chrome, Firefox, everywhere |
| **Null pointer arithmetic** | `int* p = nullptr; p + 5;` | UB, potential exploits |
| **Uninitialized memory** | `int x; sink(x);` or `Point p; sink(p.x);` | CWE-457, info leaks |
| **Uninit through variadics** | `int x; printf("%d", x);` or `consume(a, x, c);` | CWE-457 through `...` |
| **Use after move** | Access moved-from object | C++ footgun |
| **Use after free** | `*p` or `strcpy(p,...)` after `free(p)` | Memory corruption CVEs |
| **Double free** | `free(p); free(p);` | Heap corruption |
| **Mismatched deallocation** | `new int[10]; delete p;` (should be `delete[]`) | Heap corruption |
| **Resource leaks** | `FILE* f = fopen(...);` without `fclose` | File descriptor exhaustion |
| **Integer overflow in index** | `v[i + j]` without bounds | Integer overflow CVEs |
| **Negative array index** | `v[-1]` | Always out of bounds |
| **Unsorted binary search** | `binary_search` on unsorted data | Logic bugs |
| **Invalid array elements** | `forall(i, 0, n, arr[i] > 0)` violations | Data integrity bugs |

## What DTT Tracks

| Category | Capabilities |
|----------|-------------|
| **Properties** | Any field: `_size`, `_capacity`, `_nonnull`, custom |
| **Ghost State** | `@` prefix for unforgeable compile-time-only properties (`@encrypted`, `@open`) |
| **Flow** | Assignments, copies, moves, swaps |
| **Lifetime** | `free`/`delete` marks pointers invalid |
| **Buffer Pointers** | Bounds for raw pointers from arrays/`new[]`/casts |
| **Resource Leaks** | Automatic detection for `fopen`/`malloc`/`open` and 40+ C library functions |
| **Uninitialized Memory** | Scalars, pointers, POD struct members passed to functions |
| **Proofs** | `if (x > 0)` proves `x > 0` in the branch; early exit propagates negation |
| **Type Range Inference** | `unsigned char` index on `[256+]` array proven safe automatically |
| **Contracts** | `requires`, `ensures`, `old()`, boolean returns |
| **Solver** | Z3 SMT for symbolic arithmetic: `v[i + j * 2]` |
| **Quantifiers** | `forall(i, 0, n, P)`, `exists(i, 0, n, P)` |
| **Concepts** | `sorted`, `nonempty`, user-defined concepts |
| **Transitive Preconditions** | Preconditions propagate through call chains |
| **Frame Inference** | Tracks which fields functions modify |
| **Auto-Postconditions** | Infers `ensures=` from method implementations |
| **Counterexamples** | Shows concrete values when verification fails |
| **Provenance Chains** | Error notes trace full state history: constructor, push_back, clear, ... |
| **Fix Suggestions** | IDE-actionable FixItHints: `if (!v.empty())`, `if (ptr)`, `if (opt.has_value())` |
| **Variadics** | Tracks through C varargs, variadic templates, packs, and fold expressions |

## Runtime Guards Work

```cpp
std::vector<int> v = get_data();  // _size unknown

if (!v.empty()) {
    v.front();    // OK: !empty() proves _size > 0
}

v.front();        // ERROR: _size unknown here
```

## Annotation Syntax

```cpp
struct Container {
    // Precondition
    [[clang::annotate("dtt:requires=_size > 0")]]
    void pop();

    // Postcondition
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push();

    // Return value tracking
    [[clang::annotate("dtt:returns=_size")]]
    size_t size() const;

    // Boolean branch proofs
    [[clang::annotate("dtt:ensures_if_true=_size == 0")]]
    [[clang::annotate("dtt:ensures_if_false=_size > 0")]]
    bool empty() const;

    // Multi-argument constraints
    [[clang::annotate("dtt:requires=arg0 + arg1 <= _size")]]
    void copy(int start, int len);
};
```

## Annotate Your Own Types

```cpp
struct Buffer {
    char* data;
    int _size;
    int _capacity;

    [[clang::annotate("dtt:requires=_size < _capacity")]]
    void append(char c) {
        data[_size++] = c;
    }

    [[clang::annotate("dtt:requires=index < _size")]]
    char& operator[](int index) {
        return data[index];
    }
};
```

### Ghost State (`@` Prefix)

Properties starting with `@` exist only at compile-time — no runtime storage:

```cpp
struct SecureData {
    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt();

    [[clang::annotate("dtt:requires=@encrypted == 1")]]
    void send();  // Can only send after encrypt()
};
```

**Key benefit:** `@` is invalid in C++ identifiers, so state can't be forged:
```cpp
s.@encrypted = 1;  // SYNTAX ERROR — impossible to bypass
```

## Value Concepts: Collection-Wide Properties

DTT supports bounded quantifiers for verifying properties over entire collections:

```cpp
// Require all elements positive
[[clang::annotate("dtt:requires=forall(i, 0, n, arr[i] > 0)")]]
double geometric_mean(const int* arr, size_t n);

// Require sorted for binary search
[[clang::annotate("dtt:requires=sorted")]]
int binary_search(const std::vector<int>& v, int target);

// Define custom concepts
[[clang::annotate("dtt:define=searchable := nonempty && sorted")]]
[[clang::annotate("dtt:requires=searchable")]]
int safe_search(const std::vector<int>& v, int target);
```

DTT auto-infers concepts from STL operations:
```cpp
std::vector<int> v = {3, 1, 4};
binary_search(v, 3);           // ERROR: v not proven sorted

std::sort(v.begin(), v.end()); // AUTO: establishes 'sorted'
binary_search(v, 3);           // OK: sorted proven
```

See [VALUE_CONCEPTS_SPEC.md](docs/VALUE_CONCEPTS_SPEC.md) for full documentation.

## Automatic STL Tracking

DTT automatically tracks standard library types — no wrappers or includes needed:

```cpp
std::vector<int> v;
v.push_back(1);     // _size = 1
v.resize(10);       // _size = 10
v[9] = 42;          // OK
v.front();          // OK: _size > 0

std::optional<int> opt;
opt.value();        // ERROR: requires _has_value == 1
opt = 42;           // _has_value = 1
opt.value();        // OK

std::unique_ptr<int> p;
*p;                 // ERROR: requires _nonnull == 1
p = std::make_unique<int>(5);
*p;                 // OK
```

**Tracked types:**

| Category | Types |
|----------|-------|
| **Containers** | `vector`, `deque`, `list`, `set`, `map`, `stack`, `queue`, and all variants |
| **Strings** | `string`, `wstring`, `string_view` |
| **Fixed-size** | `array<T,N>`, `span<T,N>`, `bitset<N>` |
| **Optional** | `optional`, `variant` (type-safe `get`), `any` (type-safe `any_cast`), `expected` (C++23) |
| **Smart ptrs** | `unique_ptr`, `shared_ptr`, `weak_ptr` |
| **C library** | `FILE*` null tracking, resource leak detection (`fopen`/`malloc`/etc.), use-after-free |

## Automatic Resource Leak Detection

DTT automatically detects resource leaks for C library functions — no annotations required. Resource ghost fields use `when` guards with **deferred evaluation**: the ghost field is applied retroactively when an error check proves success — `if (f)` for pointers, `if (fd >= 0)` for integer fds. Unchecked results produce no false positives:

```cpp
void leak_detected() {
    FILE* f = fopen("data.txt", "r");
    if (f) {                // proves non-null → @open set to 1
        // forgot fclose → ERROR: @open still 1 at scope exit
    }
}

void correct_cleanup() {
    FILE* f = fopen("data.txt", "r");
    if (!f) return;         // proves non-null → @open set to 1
    // ... use file ...
    fclose(f);              // OK: f.@open = 0
}

void no_false_positive() {
    FILE* f = fopen("data.txt", "r");
    // No null check → @open never set → silent
}
```

**Tracked resource pairs:**

| Acquire | Release | Ghost Field |
|---------|---------|-------------|
| `fopen`, `tmpfile`, `fdopen`, `popen`, `freopen` | `fclose`, `pclose` | `@open` |
| `malloc`, `calloc`, `realloc`, `aligned_alloc`, `strdup`, `strndup` | `free` | `@allocated` |
| `open`, `openat`, `creat`, `dup`, `dup2`, `dup3`, `pipe`, `socket`, `accept`, `accept4`, `mkstemp`, `epoll_create`, `epoll_create1` | `close` | `@fd` |
| `opendir`, `fdopendir` | `closedir` | `@dir` |
| `dlopen` | `dlclose` | `@dl` |
| `mmap` | `munmap` | `@mapped` |
| `sem_open` | `sem_close` | `@sem` |
| `mq_open` | `mq_close` | `@mq` |
| `shm_open` | `close` | `@shm` |
| `iconv_open` | `iconv_close` | `@iconv` |
| `regcomp` | `regfree` | `@regex` |
| `getaddrinfo` | `freeaddrinfo` | `@addrinfo` |

DTT understands ownership transfer — returning a resource pointer is valid:
```cpp
void* allocate() {
    void* p = malloc(100);
    return p;  // OK: ownership transferred to caller
}
```

## Uninitialized Memory Detection

DTT detects uninitialized variables (CWE-457) when passed to functions:

```cpp
void sink_int(int v);
void sink_point(Point p);

void example() {
    int x;               // Uninitialized
    sink_int(x);         // ERROR: reading uninitialized variable 'x'

    Point p;             // POD struct - members indeterminate
    p.x = 1;
    sink_int(p.x);       // OK: p.x initialized
    sink_int(p.y);       // ERROR: p.y uninitialized

    Point q{};           // Brace-initialized (zero-init)
    sink_point(q);       // OK: all members initialized
}
```

DTT tracks individual members of POD structs, catching partial initialization bugs.

## Buffer Pointer Tracking

DTT tracks bounds for raw pointers derived from arrays — a "compile-time span":

```cpp
int arr[10];
int* p = arr;        // DTT knows: _size=10

p[9] = 1;            // OK: 9 < 10
p[10] = 1;           // ERROR: 10 >= 10

int* q = arr + 3;    // _size=7 (remaining elements)
q[6] = 1;            // OK: 6 < 7
q[7] = 1;            // ERROR: 7 >= 7

// Casts preserve bounds
char* c = (char*)arr;   // _size=40 bytes
c[39] = 1;           // OK
c[40] = 1;           // ERROR
```

## Three Modes

| Mode | Category C/D | Use Case |
|------|--------------|----------|
| `inform` | Warnings | Default: see issues without blocking |
| `enforce` | Errors | CI: fail on any uncertainty |
| `quiet` | Silent | Gradual adoption |

```bash
clang++ -fplugin=./libDTTPlugin.so -fplugin-arg-dtt---dtt-mode=enforce ...
```

## Philosophy

**DTT errors mean "I found a bug", not "I couldn't prove safety".**

When DTT can't reason about something (lost tracking, solver timeout), it warns by default — not errors. You're never forced to refactor working code just to satisfy the checker.

See [PHILOSOPHICAL_DESIGN_CONCERNS.md](docs/archive/PHILOSOPHICAL_DESIGN_CONCERNS.md) for the full design rationale.

## Test Suite

Comprehensive test suite covering bounds checking, nullptr tracking, buffer pointer tracking, contract propagation, Z3 integration, C varargs, variadic templates, fold expressions, and edge cases:

```bash
ctest --test-dir build                    # Run all
ctest --test-dir build -R "nullptr"       # Nullptr tests
ctest --test-dir build -L "xfail"         # Known limitations
```

## Documentation

- [GUIDE.md](GUIDE.md) — Full feature reference
- [PHILOSOPHICAL_DESIGN_CONCERNS.md](docs/archive/PHILOSOPHICAL_DESIGN_CONCERNS.md) — Design philosophy

## Requirements

| Platform | Packages |
|----------|----------|
| **Ubuntu/Debian** | `clang-18 llvm-18-dev libclang-18-dev cmake libz3-dev` |
| **macOS** | `brew install llvm@18 cmake z3` |
| **Arch** | `clang llvm cmake z3` |

### macOS Notes

Use Homebrew LLVM path:
```bash
cmake -B build \
  -DCMAKE_C_COMPILER=$(brew --prefix llvm@18)/bin/clang \
  -DCMAKE_CXX_COMPILER=$(brew --prefix llvm@18)/bin/clang++ \
  -DLLVM_DIR=$(brew --prefix llvm@18)/lib/cmake/llvm \
  -DClang_DIR=$(brew --prefix llvm@18)/lib/cmake/clang
```

## License

BSD 2-Clause. See [LICENSE](LICENSE).
