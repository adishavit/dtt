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

### How It Works

```cpp
void leak_example() {
    FILE* f = fopen("file.txt", "r");  // f.@open = 1, requires @open == 0 at scope exit
    // ... use file ...
}  // ERROR: f.@open == 1, needs fclose before scope exit

void correct_example() {
    FILE* f = fopen("file.txt", "r");
    if (f) {
        // ... use file ...
        fclose(f);  // f.@open = 0
    }
}  // OK: f.@open == 0 or f was null
```

### Tracked Resource Pairs

| Acquire | Release | Ghost Field |
|---------|---------|-------------|
| `fopen`, `tmpfile`, `fdopen`, `popen` | `fclose`, `pclose` | `@open` |
| `malloc`, `calloc`, `realloc`, `strdup`, `strndup` | `free` | `@allocated` |
| `open`, `creat`, `socket`, `accept`, `dup`, `dup2`, `mkstemp`, `epoll_create` | `close` | `@fd` |
| `opendir`, `fdopendir` | `closedir` | `@dir` |
| `dlopen` | `dlclose` | `@dl` |
| `mmap` | `munmap` | `@mapped` |
| `sem_open` | `sem_close` | `@sem` |
| `mq_open` | `mq_close` | `@mq` |
| `shm_open` | `close` | `@shm` |
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

To add a new resource-acquiring function:

```cpp
addResourceAcquire("function_name", arity, "@ghost_field");
```

To add a new resource-releasing function:

```cpp
addResourceRelease("function_name", arity, "@ghost_field");
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
- External library test suite: 218 tests across 10 libraries (zlib, libarchive, libuv, libusb, libssh2, libpng, FFmpeg, libdbus, s2n-tls, cpp-httplib) with 0 false positive warnings
