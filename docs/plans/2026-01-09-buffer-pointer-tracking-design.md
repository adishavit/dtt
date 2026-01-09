# DTT Buffer Pointer Tracking — Design

**Status:** Proposed
**Date:** 2026-01-09

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
[[clang::annotate("dtt:ptr_bounds=buf:len")]]
void process(int* buf, size_t len) {
    // buf._size = len (symbolic)

    if (len > 5) {
        buf[4] = 0;    // OK: Z3 proves 4 < len given len > 5
    }

    for (size_t i = 0; i < len; i++) {
        buf[i] = 0;    // OK: loop invariant i < len
    }

    buf[len - 1] = 0;  // WARNING: len > 0 not proved
}

[[clang::annotate("dtt:ptr_bounds=buf:len")]]
[[clang::annotate("dtt:requires=len > 0")]]
void process_nonempty(int* buf, size_t len) {
    buf[len - 1] = 0;  // OK: requires guarantees len > 0
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

## Function Parameters

### Unannotated Functions
```cpp
void foo(int* q);
foo(p);                // Inside foo: q is unknown (Category C)
```

### Annotated Functions
```cpp
// Establish pointer bounds from parameter
[[clang::annotate("dtt:ptr_bounds=buf:len")]]
void process(int* buf, size_t len);

// Inside process: buf has _size=len
```

### Tracking Loss Behavior

Follows existing DTT philosophy (Category C):
- No warning at loss point
- Warning at first unsafe use
- Note points back to where tracking was lost

---

## Annotation Syntax

### Preconditions on Pointer Bounds

```cpp
// Element-based
[[clang::annotate("dtt:requires=buf._size >= n")]]
void process(int* buf, size_t n);

// Byte-based
[[clang::annotate("dtt:requires=dest._size_bytes >= count")]]
[[clang::annotate("dtt:requires=src._size_bytes >= count")]]
void* memcpy(void* dest, const void* src, size_t count);
```

### Establishing Bounds from Parameters

```cpp
[[clang::annotate("dtt:ptr_bounds=buf:len")]]
void process(int* buf, size_t len) {
    buf[len - 1] = 0;  // OK
    buf[len] = 0;      // ERROR
}
```

---

## Integration with Existing DTT

| Feature | Interaction |
|---------|-------------|
| nullptr tracking | Buffer state and null state are mutually exclusive |
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

error: DTT: memcpy count 44 exceeds dest._size_bytes (40)
   20 |     memcpy(arr, src, 44);
      |     ^
```

---

## Scope

### v1 (Initial)
- Stack arrays with compile-time size
- `new T[N]` with constant or symbolic N
- Pointer arithmetic tracking (constant and symbolic offsets)
- Symbolic bounds checking via Z3
- `_size` / `_size_bytes` in annotations
- Type cast handling

### v2+ (Future)
- `malloc` idiom recognition
- Annotated function return values (`dtt:returns_ptr_bounds`)
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
