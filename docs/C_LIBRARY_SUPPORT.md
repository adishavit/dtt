# C Library Support

> **STATUS: IMPLEMENTED** (v34)
>
> DTT provides comprehensive C library support including bounds checking and resource leak detection.
> See `annotations/libc.dtt` for the full list of tracked functions.

---

## Overview

DTT automatically tracks C library functions in two categories:

1. **Bounds Checking** - Detects buffer overflows in memory/string functions
2. **Resource Leak Detection** - Detects unclosed files, memory leaks, etc.

No annotations required - detection is automatic.

---

## Bounds Checking

### Memory Functions

| Function | Preconditions | CVE Examples |
|----------|---------------|--------------|
| `memcpy(dst, src, n)` | `n <= dst._size_bytes`, `n <= src._size_bytes` | CVE-2023-38545 (curl) |
| `memmove(dst, src, n)` | `n <= dst._size_bytes`, `n <= src._size_bytes` | CVE-2022-26937 (Windows NFS) |
| `memset(dst, c, n)` | `n <= dst._size_bytes` | Buffer overflow CVEs |
| `memcmp(s1, s2, n)` | `n <= s1._size_bytes`, `n <= s2._size_bytes` | Over-read CVEs |

### String Functions

| Function | Preconditions | Notes |
|----------|---------------|-------|
| `strcpy(dst, src)` | `strlen(src) + 1 <= dst._size_bytes` | Moonlight GHSA pattern |
| `strncpy(dst, src, n)` | `n <= dst._size_bytes` | Bounded but may not null-terminate |
| `strcat(dst, src)` | `strlen(dst) + strlen(src) + 1 <= dst._size_bytes` | |
| `strncat(dst, src, n)` | `strlen(dst) + n + 1 <= dst._size_bytes` | |

### Format Functions

| Function | Behavior | Notes |
|----------|----------|-------|
| `sprintf(buf, fmt, ...)` | **Always warns** | Unbounded, use snprintf |
| `snprintf(buf, n, fmt, ...)` | `n <= buf._size_bytes` | CVE-2018-18820 (Icecast) |
| `vsprintf` | **Always warns** | Unbounded |
| `vsnprintf(buf, n, ...)` | `n <= buf._size_bytes` | Safe when used correctly |

### Pointer Arithmetic

DTT tracks buffer capacity through pointer arithmetic:

```cpp
char buf[100];           // buf._size_bytes = 100
char* p = buf + 20;      // p._size_bytes = 80
memcpy(p, src, 90);      // ERROR: 90 > 80
```

---

## Resource Leak Detection

DTT enforces linear type semantics: resources acquired must be released before scope exit.

Resource ghost fields use **`when` guards** with **deferred evaluation** for sound semantics. When a resource-acquiring function is called, the ghost field postcondition is stored as *pending* because the guard condition isn't known yet. When control flow later proves the guard — `if (f)` proves `_nonnull != 0` for pointers, `if (fd >= 0)` proves `_value >= 0` for integer fds — the postcondition is applied retroactively. Unchecked results produce no false positives.

### How It Works

```cpp
void leak_detected() {
    FILE* f = fopen("file.txt", "r");
    if (f) {                // proves non-null → @open = 1 applied
        // forgot fclose → ERROR: @open still 1 at scope exit
    }
}

void correct_example() {
    FILE* f = fopen("file.txt", "r");
    if (!f) return;         // proves non-null → @open = 1 applied
    // ... use file ...
    fclose(f);              // f.@open = 0
}  // OK: @open == 0

void checked_example() {
    FILE* f = fopen("file.txt", "r");
    if (f) {                // proves non-null → @open = 1 applied
        // ... use file ...
        fclose(f);          // f.@open = 0
    }
}  // OK

void no_false_positive() {
    FILE* f = fopen("file.txt", "r");
    // No null check → guard never proven → @open never set → silent
}
```

### Tracked Resource Pairs

| Acquire | Release | Ghost Field |
|---------|---------|-------------|
| `fopen`, `tmpfile`, `fdopen`, `popen` | `fclose`, `pclose` | `@open` |
| `malloc`, `calloc`, `realloc`, `aligned_alloc`, `strdup`, `strndup` | `free` | `@allocated` |
| `open`, `openat`, `creat`, `dup`, `dup2`, `dup3`, `socket`, `accept`, `accept4`, `mkstemp`, `epoll_create`, `epoll_create1` | `close` | `@fd` |
| `opendir`, `fdopendir` | `closedir` | `@dir` |
| `dlopen` | `dlclose` | `@dl` |
| `mmap` | `munmap` | `@mapped` |
| `sem_open` | `sem_close` | `@sem` |
| `mq_open` | `mq_close` | `@mq` |
| `shm_open` | `close` | `@fd` |
| `iconv_open` | `iconv_close` | `@iconv` |
| `pthread_mutex_init` | `pthread_mutex_destroy` | `@mutex` |
| `pthread_cond_init` | `pthread_cond_destroy` | `@cond` |
| `pthread_rwlock_init` | `pthread_rwlock_destroy` | `@rwlock` |

### DIR* Null Checking

Directory stream operations require non-null DIR* (catches using result of `opendir()` without null check):

```cpp
DIR* d = opendir("/tmp");
readdir(d);    // ERROR: DIR* may be null (opendir can fail)

if (d) {
    readdir(d);  // OK: proven non-null
    closedir(d);
}
```

Tracked operations: `readdir`, `telldir`, `rewinddir`, `seekdir`.

### Ownership Transfer

Returning a resource pointer transfers ownership to the caller:

```cpp
void* allocate(size_t n) {
    void* p = malloc(n);  // p.@allocated = 1
    return p;             // OK: ownership transferred
}
```

### Alias Handling

DTT tracks resource identity, so closing via any alias works:

```cpp
FILE* f = fopen("file.txt", "r");
FILE* g = f;    // g points to same resource
fclose(g);      // OK: resource is closed
// f.@open is now 0 (same resource)
```

---

## FILE* Operations

### Null Checking

FILE* functions require non-null pointers:

```cpp
FILE* fp = fopen("file.txt", "r");  // May return NULL
fread(buf, 1, n, fp);               // ERROR: fp may be null

if (fp) {
    fread(buf, 1, n, fp);           // OK: fp proven non-null
    fclose(fp);
}
```

### Read/Write Mode Tracking

```cpp
FILE* fp = fopen("file.txt", "r");  // fp._readable = 1, fp._writable = 0
fwrite(data, 1, n, fp);             // ERROR: fp not opened for writing
```

---

## Implementation Details

All C library annotations are defined in `annotations/libc.dtt` using the declarative `.dtt` format and loaded into the `FreeFunctionAnnotations` registry at startup.

### Data Structures

```cpp
// Function identification
struct FreeFunctionKey {
    const char* functionName;
    unsigned arity;
};

// Function metadata
struct FreeFunctionInfo {
    SmallVector<FreeFunctionPrecondition, 2> preconditions;
    SmallVector<FreeFunctionPostcondition, 2> postconditions;
    SmallVector<FreeFunctionResultPost, 2> resultPostconditions;
    SmallVector<FreeFunctionScopeExitReq, 2> scopeExitRequires;
};
```

### Adding New Functions

Add annotations to `annotations/libc.dtt`. Multiple functions sharing the same body can be grouped:

```
# Pointer-returning resources (guard on null check)
fn new_acquire(2), other_acquire(1) {
  ensures result.@ghost = FileIO.OPEN when result._nonnull != 0
  scope_exit result.@ghost == FileIO.CLOSED when result._nonnull != 0
}

# Integer fd-returning resources (guard on >= 0 check)
fn fd_acquire(1) {
  ensures result.@fd = FileIO.OPEN when result._value >= 0
  scope_exit result.@fd == FileIO.CLOSED when result._value >= 0
}

# Release function
fn release(1) {
  ensures arg0.@ghost = FileIO.CLOSED
}
```

---

## Test Coverage

| Category | Tests |
|----------|-------|
| Bounds checking | `810-811` (memcpy), `874-881` (all functions) |
| Resource leaks | `988` (fopen patterns), `1012-1013` (auto detection) |
| Heartbleed patterns | `884-888` (source over-read) |

---

## Related Documentation

- [RESOURCE_TRACKING_DESIGN.md](archive/RESOURCE_TRACKING_DESIGN.md) - Resource-centric design
- [archive/PHASE1_C_MEMORY_FUNCTIONS.md](archive/PHASE1_C_MEMORY_FUNCTIONS.md) - Original bounds checking spec
- [HEARTBLEED_AUTOTRACK_SPEC.md](archive/HEARTBLEED_AUTOTRACK_SPEC.md) - Heartbleed detection spec

---

## Remaining Gaps

| Gap | Priority | Notes |
|-----|----------|-------|
| `scanf` bounds | P2 | Format string parsing needed |
| Allocator wrappers | P2 | Custom allocator annotation support |
| Taint tracking | P3 | Mark untrusted input sources |

**Closed gaps (v35+):**
- Double-close detection via `_nonnull` invalidation
- Double-free and use-after-free detection via `_lifetime` tracking
- Early return proof preservation (GlobalProofs save/restore)
- Generic UAF: all pointer args checked at all call sites (replaces per-function table)
- DIR* null checking: `readdir`, `telldir`, `rewinddir`, `seekdir`
- Pthread lifecycle: `pthread_mutex_init/destroy`, `pthread_cond_init/destroy`, `pthread_rwlock_init/destroy`
- Resource tracking: `mkstemp`, `epoll_create`, `epoll_create1`
- STL `at()` bounds checking for `array`, `vector`, `deque`, `basic_string`, `basic_string_view`
- STL `pop_front()` / adapter `pop()` now decrement `_size` (was only checking nonEmpty)
- `&expr` satisfies `_nonnull` preconditions: `inferNonnullFromExpr()` consulted before TypedEnv lookup, eliminating FPs for `&stack_var`, string literals, and array decay (919 FPs eliminated across 7 libraries)
- Arity mismatch diagnostic: warns when function name matches a `.dtt` annotation but call arity differs (catches macro-expanded signatures, wrong arities in annotation files)
- `.dtt` postcondition ordering: postconditions now applied after argument invalidation, preventing ghost field loss on annotated functions
- `.dtt` frame semantics: `hasDTTAnnotations()` now recognizes `.dtt` file annotations via `FreeFunctionAnnotatedNames`, preventing argument invalidation that wiped ghost fields on `.dtt`-annotated function calls
- External library test suite across 10 libraries (zlib, libarchive, libuv, libusb, libssh2, libpng, FFmpeg, libdbus, s2n-tls, cpp-httplib) with 0 false positive warnings
- Deprecated bare `dtt:field=value` annotation syntax removed (replaced by `dtt:precondition` / `dtt:postcondition`)
