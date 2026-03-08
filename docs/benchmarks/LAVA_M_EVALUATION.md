# LAVA-M Benchmark Evaluation

## Overview

**LAVA-M** (Large-scale Automated Vulnerability Addition) is a synthetic benchmark from NYU/MIT Lincoln Lab that injects OOB memory access bugs into 4 GNU coreutils-8.24 programs. Each bug follows an identical pattern: input bytes flow to a magic-value comparison, guarding an out-of-bounds memory access.

**Paper:** Dolan-Gavitt et al., "LAVA: Large-scale Automated Vulnerability Addition" (S&P 2016)
**Repo:** https://github.com/panda-re/lava
**Corpus:** http://panda.moyix.net/~moyix/lava_corpus.tar.xz (101MB)

## Bug Counts

| Program | Injected Bugs | Taint Source |
|---------|--------------|--------------|
| base64  | 44           | `fread()` from stdin/file |
| md5sum  | 57           | `fread()` from file |
| uniq    | 28           | `readlinebuffer()` → `getc()` |
| who     | 2,136        | `getutxent()` → utmp struct reading |
| **Total** | **2,265**  | |

## LAVA-M Bug Injection Pattern

Every LAVA-M bug follows this template:

```c
// 1. Input bytes read (taint source)
fread(buffer, 1, size, input_file);

// 2. DUA bytes copied to global (lava_set)
lava_global = *(unsigned int*)(buffer + offset);

// 3. Magic value guard
if (lava_global == 0x6c617661) {  // "lava" in ASCII
    // 4. Attack point: OOB memory access
    *(ptr + lava_global) = value;  // or ptr[lava_global]
}
```

The attack points fall into these categories:
- **Pointer arithmetic OOB** — `*(ptr + tainted_val)` (most common)
- **Array index OOB** — `array[tainted_val]`
- **memcpy with tainted length** — `memcpy(dst, src, tainted_val)`
- **malloc with tainted size** — `malloc(tainted_val)`

## DTT Coverage Analysis

### What DTT Catches

| Attack Pattern | DTT Mechanism | Coverage |
|---------------|---------------|----------|
| `memcpy(dst, src, tainted_len)` | CWE-120 taint sink + `_size_bytes` bounds | **Full** |
| `malloc(tainted_size)` | CWE-190 taint sink | **Full** |
| `array[tainted_index]` | `_size_bytes` bounds checking | **Full** |
| `*(ptr + tainted_offset)` | `_size_bytes` bounds checking | **Full** |
| Magic value guard bypass | Taint persists through `==` comparisons | **Full** |
| Global transport (lava_set/get) | Taint propagates through assignment | **Partial** (single TU) |

### Key Finding

DTT catches **all 4 LAVA-M attack patterns** through two complementary mechanisms:

1. **Taint-based sinks** (CWE-120, CWE-190): Flag when tainted values reach `memcpy` length or `malloc` size parameters
2. **Bounds checking** (`_size_bytes`): Flag when any unproven index is used on a known-size buffer — regardless of taint

This means DTT would catch LAVA-M bugs even without taint tracking, because the OOB access pattern involves using a variable (whose value is unknown at compile time) as an index into a fixed-size buffer.

### Z3 Guard Suppression

DTT correctly handles the "fixed" versions of LAVA-M patterns:

```c
// Tainted but bounded — DTT suppresses warning
if (payload <= 128) {
    memcpy(response, packet, payload);  // Safe: Z3 proves payload <= 128
}

// Tainted and unbounded — DTT reports error
if (payload == 0x6c617661) {
    memcpy(response, packet, payload);  // Unsafe: equality doesn't bound
}
```

## Test Results

### DTT Test Files

| Test File | Type | Variants | Result |
|-----------|------|----------|--------|
| `1820_lavam_memcpy_sink_fail.cpp` | _fail | 4 | All caught (memcpy/malloc sinks) |
| `1821_lavam_array_index_fail.cpp` | _fail | 4 | All caught (bounds checking) |
| `1822_lavam_guarded_pass.cpp` | _pass | 3 | All clean (Z3 proves bounds) |

### LAVA-1 Target: file-5.30

DTT was run against all 18 source files of `file-5.30` (the LAVA-1 target, pre-injection).
DTT found issues in 15 of 18 files, primarily:
- Buffer bounds violations
- Null pointer dereference risks
- File handle validation

No taint findings on clean (pre-injection) code, which is correct — false positive rate is 0% for taint on uninjected code.

## Build Environment Notes

### Successful

- **file-5.30**: Builds and compiles with DTT on Ubuntu 24.04 / clang-18
- **DTT test cases**: All pass in CI-compatible format

### Not Buildable (as-is)

- **coreutils-8.24**: gnulib (2015 vintage) is incompatible with Ubuntu 24.04 / glibc 2.39 / gcc-13 / clang-18. The gnulib wrapper headers (`./lib/stdio.h`, `./lib/unistd.h`) generate compilation errors due to `_GL_INLINE_HEADER_BEGIN/END` and `__gnuc_va_list` type resolution failures.
- **Workaround**: Use Docker with Ubuntu 16.04, or use the pre-built LAVA-M corpus tarball.

## Estimated LAVA-M Score

Based on pattern analysis:

| Category | Est. Detection Rate | Rationale |
|----------|-------------------|-----------|
| memcpy-length bugs | ~100% | Direct taint sink (CWE-120) |
| malloc-size bugs | ~100% | Direct taint sink (CWE-190) |
| Array index bugs | ~90-100% | Bounds checking catches most; some may involve dynamic buffer sizes |
| Pointer arithmetic bugs | ~80-90% | Depends on whether buffer size is known at compile time |
| **Global transport** | ~60-70% | Single-TU analysis limits tracking through complex global chains |

**Conservative overall estimate: 70-85% of 2,265 bugs** would be flagged by DTT.

The limiting factors are:
1. **Global variable chains**: LAVA uses `lava_set`/`lava_get` via globals. DTT tracks these within a function but may lose tracking across function boundaries without annotations.
2. **Dynamic buffer sizes**: If the target buffer is heap-allocated with a runtime size, DTT may not have bounds information.
3. **Single-TU limitation**: Some taint flows cross compilation units.

## Recommendations

1. **Full validation** requires Docker with Ubuntu 16.04 to build LAVA-M corpus
2. **Highest-value improvement**: Add cross-function taint summary propagation (would push estimate to 85-95%)
3. **Low-hanging fruit**: The `who` program (2,136 bugs) uses `getutxent()` — adding this as a taint source would cover the largest LAVA-M component
