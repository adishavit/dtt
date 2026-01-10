# DTT Buffer Pointer Tracking — Design

**Status:** Implemented

## Overview

Extend DTT to track bounds for raw pointers derived from arrays, enabling compile-time detection of buffer overflows — a "compile-time span".

---

## Tracking Model

### Typed Pointers (`T*`)

**Stored:** `(_base_length, _offset)`

| Property | Type | Description |
|----------|------|-------------|
| `_base_length` | Internal | Total elements in original buffer |
| `_offset` | Internal | Current position (elements from start) |
| `_size` | User-facing | `_base_length - _offset` (elements remaining) |
| `_size_bytes` | User-facing | `_size * sizeof(T)` (bytes remaining) |

Internal properties are for diagnostics only. User-facing properties are used in annotations.

### Void Pointers (`void*`)

**Stored:** `(_size_bytes)` only — element tracking lost at cast

---

## Symbolic and Runtime Tracking

Buffer sizes can be compile-time constants or symbolic runtime values. DTT uses Z3 to prove bounds at compile time, just as it does for container properties.

### Compile-Time Constants

```cpp
int arr[10];           // _base_length = 10 (constant)
int* p = arr + 3;      // _size = 7 (constant)

p[6] = 0;              // OK: 6 < 7 (trivially proved)
p[7] = 0;              // ERROR: 7 >= 7 (trivially disproved)
```

### Symbolic Runtime Values

```cpp
size_t n = get_size();
int* p = new int[n];   // _base_length = n (symbolic)

p[0] = 0;              // WARNING: n > 0 not proved (Category D)

if (n > 0) {
    p[0] = 0;          // OK: Z3 proves 0 < n given n > 0
}
```

### Symbolic Expressions in Functions

```cpp
[[clang::annotate("dtt:requires=buf._size >= len")]]
void process(int* buf, size_t len) {
    // requires establishes: buf._size >= len, buf._offset = 0

    if (len > 5) {
        buf[4] = 0;    // OK: Z3 proves 4 < buf._size given buf._size >= len > 5
    }

    for (size_t i = 0; i < len; i++) {
        buf[i] = 0;    // OK: loop invariant i < len <= buf._size
    }

    buf[len - 1] = 0;  // WARNING: len > 0 not proved
}

[[clang::annotate("dtt:requires=buf._size >= len")]]
[[clang::annotate("dtt:requires=len > 0")]]
void process_nonempty(int* buf, size_t len) {
    buf[len - 1] = 0;  // OK: len > 0 proved by requires
}
```

### Pointer Arithmetic with Symbolic Offsets

```cpp
size_t n = get_size();
int* p = new int[n];   // _base_length = n, _offset = 0

size_t k = get_offset();
int* q = p + k;        // _base_length = n, _offset = k
                       // _size = n - k (symbolic expression)

if (k < n) {
    q[0] = 0;          // OK: Z3 proves 0 < n - k given k < n
}
```

---

## Pointer States

A pointer is exactly one of:

| State | Description | `*p` | `p[n]` |
|-------|-------------|------|--------|
| `buffer(_base_length, _offset)` | Known bounds, implicitly non-null | Check offset | Check offset+n |
| `null` | Known nullptr | ERROR | ERROR |
| `maybe-null` | Possibly null (existing) | Warning | Warning |
| `unknown` | Lost tracking (Category C) | Warning | Warning |

States are mutually exclusive. `p = nullptr` clears buffer tracking entirely.

---

## Triggering Syntax (v1)

What creates a tracked pointer:

```cpp
int arr[10];           // Array itself tracked
int* p1 = arr;         // Array decay
int* p2 = &arr[0];     // Address of element
int* p3 = &arr[5];     // Address of middle (_offset=5)
int* p4 = arr + 3;     // Array + arithmetic (_offset=3)
int* p5 = new int[10]; // new[] expression
```

**Deferred (v2+):** `malloc`, annotated function returns, "blessing" syntax

---

## Pointer Arithmetic

| Operation | Result |
|-----------|--------|
| `p + n` | `_offset += n` |
| `p - n` | `_offset -= n` |
| `&p[n]` | `_offset += n` |
| `p++` / `++p` | `_offset += 1` |
| `p--` / `--p` | `_offset -= 1` |
| `p += n` | `_offset += n` |
| `p -= n` | `_offset -= n` |
| `p = p + n` | `_offset += n` |
| `p = q + n` | Copy from q, `_offset += n` |

### Bounds on Arithmetic (per C++ standard)

| Resulting offset | On creation | On deref |
|------------------|-------------|----------|
| `0` to `_base_length - 1` | OK | OK |
| `_base_length` (one-past-end) | OK | ERROR |
| `< 0` or `> _base_length` | **ERROR** (UB) | ERROR |

One-past-end is valid for iterator idioms. Before-start is always UB per the C++ standard.

---

## Type Casts

### `int*` to `char*`
```cpp
int* p = arr + 3;      // _size=7 elements
char* c = (char*)p;    // _size=28 chars (recomputed from bytes)
```

### `int*` to `void*`
```cpp
int* p = arr + 3;      // _size=7, _size_bytes=28
void* v = p;           // _size_bytes=28 (element tracking lost)
```

### `void*` to `int*`
```cpp
void* v = ...;         // _size_bytes=28
int* p = (int*)v;      // _size = 28 / sizeof(int) = 7
```

---

## Annotation Syntax

DTT uses the existing `requires` annotation with pointer properties. The `requires` annotation has **dual semantics** for pointer parameters:

1. **Call site:** Check constraint using caller's tracking
2. **Function body:** Assume constraint holds, establish tracking

### Element-based Bounds

```cpp
[[clang::annotate("dtt:requires=buf._size >= n")]]
void process(int* buf, size_t n);
```

**At call site:**
```cpp
int arr[10];
process(arr, 5);    // Check: arr._size >= 5 → 10 >= 5 ✓
process(arr, 15);   // Check: arr._size >= 15 → 10 >= 15 ✗ ERROR
```

**Inside function:**
- `buf._offset = 0` (at start of accessible region)
- `buf._size >= n` (constraint assumed true)
- `buf[n-1] = 0` is OK (n-1 < n <= buf._size)

### Byte-based Bounds (for memcpy, etc.)

```cpp
[[clang::annotate("dtt:requires=dest._size_bytes >= count")]]
[[clang::annotate("dtt:requires=src._size_bytes >= count")]]
void* memcpy(void* dest, const void* src, size_t count);
```

### Untracked Pointers at Call Site

If caller passes an untracked pointer, DTT cannot verify the constraint:

```cpp
char* unknown = get_buffer();   // No tracking (Category A/B)
memcpy(unknown, src, 100);      // WARNING: unknown._size_bytes is unknown
                                // Category C: can't verify requires
```

This is the correct behavior — DTT warns about unverifiable constraints.

### `>=` vs `==` Semantics

| Annotation | Call site | Inside function |
|------------|-----------|-----------------|
| `buf._size >= len` | Buffer at least len | Access [0..len-1] safe |
| `buf._size == len` | Buffer exactly len | Access [0..len-1] safe, len is OOB |

Use `>=` for flexibility (caller can pass larger buffer).
Use `==` for exact sizing requirements.

---

## Function Parameters

### Unannotated Functions

```cpp
void foo(int* q);
foo(p);                // Inside foo: q is unknown (Category C)
```

### Annotated Functions

```cpp
[[clang::annotate("dtt:requires=buf._size >= len")]]
void process(int* buf, size_t len) {
    // buf._offset = 0, buf._size >= len (established by requires)
    buf[len - 1] = 0;  // OK
    buf[len] = 0;      // ERROR: len >= buf._size when buf._size == len
}
```

### Tracking Loss Behavior

Follows existing DTT philosophy (Category C):
- No warning at loss point
- Warning at first unsafe use
- Note points back to where tracking was lost

### Pointer Parameter Semantics

When passing pointers to functions, DTT distinguishes between what can and cannot change:

| Parameter Type | Can Change Pointer? | Can Change Pointed-To? | Invalidate Tracking? |
|----------------|---------------------|------------------------|---------------------|
| `f(int* p)` | No (by value) | Yes (`*p = x`) | **No** |
| `f(int*& p)` | Yes (reference) | Yes | Yes |
| `f(int** pp)` | Yes (via `*pp`) | Yes | Yes |
| `f(T* ptr)` with `&obj` | N/A | Yes (`ptr->field`) | Yes (for obj) |

**Key insight:** Passing `p` by value to `f(int* p)` gives the function a *copy* of the pointer. The local variable `p` cannot change — it still points to the same buffer with the same bounds.

**Dangling vs. Nullability:** The function *can* `delete`/`free` the memory, creating a dangling pointer. But:
- A dangling pointer is not null — it's a *lifetime* issue
- DTT tracks nullability and buffer bounds, not memory lifetime
- This is a separate class of bug (use-after-free)

```cpp
int* p = new int[10];  // p: non-null, _size=10
unknown_func(p);       // p still non-null, _size still 10
p[5] = 1;              // OK: tracking preserved
// (If unknown_func deleted p, this is use-after-free — not DTT's domain)
```

---

## Integration with Existing DTT

| Feature | Interaction |
|---------|-------------|
| `nullptr` tracking | Buffer state and null state are mutually exclusive |
| Category C (lost tracking) | Same behavior — warn at first unsafe use |
| Three modes | `inform`, `enforce`, `quiet` apply to buffer warnings |
| `std::span`, `std::array` | Already tracked; `data()` propagates bounds to pointer |

---

## Diagnostics

```
error: DTT: buffer access at index 7, but _size is 7
       (pointer at offset 3 into 10-element buffer)
   15 |     p[7] = 0;
      |     ^

error: DTT: memcpy requires src._size_bytes >= count
       but src._size_bytes is unknown
   20 |     memcpy(dest, unknown_src, 100);
      |     ^
note: tracking lost at initialization from external function
```

---

## Real-World Example: Heartbleed (CVE-2014-0160)

The Heartbleed vulnerability in OpenSSL allowed attackers to read 64KB of server memory (passwords, private keys) by sending a malformed heartbeat request. The bug: a `memcpy` with an attacker-controlled length and no bounds check.

### The Difference

| Original (Vulnerable) | With DTT |
|----------------------|----------|
| | `[[clang::annotate("dtt:requires=src._size_bytes >= count")]]` |
| `void* memcpy(void* dest, const void* src, size_t count);` | `void* memcpy(void* dest, const void* src, size_t count);` |

**One annotation. Zero application code changes.**

### The Vulnerable Code (unchanged)

```c
int tls1_process_heartbeat(SSL *s) {
    unsigned char *p = &s->s3->rrec.data[0], *pl;
    unsigned short hbtype;
    unsigned int payload;

    hbtype = *p++;
    n2s(p, payload);
    pl = p;              // pl: UNTRACKED (from struct field)

    buffer = OPENSSL_malloc(1 + 2 + payload + 16);
    bp = buffer;

    *bp++ = TLS1_HB_RESPONSE;
    s2n(payload, bp);
    memcpy(bp, pl, payload); // DTT ERROR: pl._size_bytes unknown
}
```

### DTT Output

```
error: DTT: memcpy requires src._size_bytes >= count
       but pl._size_bytes is unknown
   |     memcpy(bp, pl, payload);
   |     ^~~~~~~~~~~~~~~~~~~~~~~
note: pl initialized from struct field access (not tracked)
note: add bounds check: if (payload <= rrec.length - 3)
```

DTT catches the bug because `pl` is untracked (derived from struct field), and the `requires` constraint cannot be verified. The warning alerts the developer to add a bounds check.

---

## Scope

### v1 (Initial) — Implementation Status

| Feature | Status | Tests | Notes |
|---------|--------|-------|-------|
| Stack arrays (compile-time) | ✅ | 400-401 | Array decay, address-of |
| `new T[N]` constant N | ✅ | 408-409 | Constant size tracked |
| `new T[N]` symbolic N | ✅ | 410, 417 | Z3 checks guards, warns if unproved |
| Pointer arithmetic from array | ✅ | 402-407 | `arr + k` bounds-checked |
| Pointer arithmetic from ptr | ✅ | 428-429 | `p + k` bounds-checked |
| Pointer copy assignment | ✅ | 416 | `q = p` transfers tracking |
| Type cast handling | ✅ | 411-412, 423-424 | Byte count preserved |
| `_size` in requires | ✅ | 420-422, 425 | Arrays and pointers |
| Negative indices (interior) | ✅ | 426-427 | `p[-1]` for `p = arr + k` |
| Global/static arrays | ✅ | — | Same as local arrays |
| nullptr + buffer | ✅ | 413 | Null check before bounds |
| Buffer state restore | ✅ | 414 | Reassignment updates tracking |
| Pointer by-value to function | ✅ | 170-171 | `f(int* p)` preserves tracking |
| Loop invariants | ✅ | 433-436 | `for (i < n)`, `for (i < n-1)` via VarBoundProof |
| 2D/3D/N-dim array rows | ✅ | 430, 437-439, 444-445, 448-449 | `arr[i]` on `T[M][N]` tracks N elements |
| Struct member arrays | ✅ | 431, 440-443, 446-447 | `s.member` for `struct { T m[N]; }` |
| Struct pointer + arrays | ✅ | 450-451 | `p->member` through pointer |
| `new` struct with arrays | ✅ | 452-455 | `new Packet` with array members |
| Pointer increment `p++/++p` | ✅ | 480-484 | Increment/decrement updates offset |
| Pointer reassignment `p = p + n` | ✅ | 485-486 | `p = p + n`, `p += n` updates offset |
| Dynamic offset `p += k` | ✅ | 485-486 | Runtime k with if-proof bounds checking |
| Ternary operator | ❌ | **432_xfail** | Merge tracking from branches (Hard) |

### v2+ (Future)
- `malloc` idiom recognition
- Ternary operator merging
- Annotated function return values
- "Blessing" syntax for unknown pointers
- Propagation from `span::data()`, etc.

---

## Key Design Decisions

1. **Implicit tracking** — no wrapper types required
2. **Absolute model** — store `(_base_length, _offset)`, derive `_size`
3. **Standard-aligned naming** — `_size`, `_size_bytes` match `std::span`
4. **Asymmetric bounds** — one-past-end OK, before-start is UB
5. **Null replaces buffer** — mutually exclusive states
6. **Category C on loss** — warn at use, not at loss point
7. **Symbolic tracking** — bounds can be runtime values, proved via Z3
8. **Dual-semantics requires** — checks at call site, establishes inside function
9. **Existing syntax only** — uses `requires` with `_size`/`_size_bytes`, no new keywords
10. **Pointer by-value preserves tracking** — `f(int* p)` cannot change local `p`; dangling is lifetime, not nullability
