# DARPA CGC Benchmark Evaluation

## Overview

**DARPA CGC** (Cyber Grand Challenge) is a corpus of ~213 challenge binaries originally designed for the 2016 autonomous security competition. Each challenge contains intentionally vulnerable C programs with known CWE-annotated vulnerabilities.

**Source:** https://github.com/trailofbits/cb-multios (Linux-portable version)
**Original:** https://github.com/CyberGrandChallenge/samples (DECREE OS)
**CWE Metadata:** https://github.com/lungetech/cgc-challenge-corpus
**Size:** 243 challenges (226 with compilable source), ~21K files total

## CGC Architecture

The challenges originally targeted DECREE, a minimal OS with 7 syscalls. The `cb-multios` port replaces these with POSIX equivalents:

| CGC Syscall | POSIX Equivalent | DTT Annotation |
|-------------|-----------------|----------------|
| `cgc_receive(fd, buf, len, rx)` | `read()` | Taint source (via `read`) |
| `cgc_transmit(fd, buf, len, tx)` | `write()` | N/A |
| `cgc_allocate(len, x, &addr)` | `mmap()` | Not yet annotated |
| `cgc_deallocate(addr, len)` | `munmap()` | Not yet annotated |

## CWEs Represented

| CWE | Description | DTT Coverage |
|-----|-------------|--------------|
| CWE-119 | Buffer bounds violation | **Full** (bounds checking) |
| CWE-120 | Buffer copy without size check | **Full** (memcpy sink) |
| CWE-121 | Stack-based buffer overflow | **Full** (bounds checking) |
| CWE-122 | Heap-based buffer overflow | **Full** (bounds checking) |
| CWE-129 | Improper array index validation | **Full** (bounds checking) |
| CWE-134 | Format string vulnerability | **Full** (taint sink) |
| CWE-190 | Integer overflow | **Partial** |
| CWE-416 | Use-after-free | **Full** |
| CWE-476 | NULL pointer dereference | **Full** |
| CWE-788 | Access after end of buffer | **Full** (bounds checking) |
| CWE-822 | Untrusted pointer dereference | **Full** |
| CWE-823 | Out-of-range pointer offset | **Full** |
| CWE-824 | Uninitialized pointer | **Full** |
| CWE-843 | Type confusion | **Partial** (variant tracking) |

## Scan Results

### Overall

| Metric | Value |
|--------|-------|
| **Total challenges** | 226 (with source) |
| **Challenges with findings** | 196 |
| **Challenges clean** | 30 (25 have 0 source files) |
| **Detection rate** | **86.7%** |
| **Effective rate** (challenges with source) | **196/201 = 97.5%** |

### Finding Categories

| Category | Count | Description |
|----------|------:|-------------|
| Null pointer dereference | 26,522 | Pointer used without null check after allocation |
| Buffer index out-of-bounds | 2,340 | Array access with unproven index |
| Uninitialized variable/pointer | 1,802 | Use before initialization |
| Union member access | 243 | Reading inactive union member |

### Analysis

The dominant finding is **null pointer dereference** — CGC challenges heavily use `cgc_allocate()` (which can fail) and custom allocators without null checks. This is a genuine vulnerability pattern: the CGC competition specifically tests whether tools can find paths where allocation failure leads to crashes.

Buffer index findings are the second most common, reflecting the core CGC vulnerability type: input-controlled array/pointer accesses without bounds validation.

## Limitations

### 1. CGC-specific Functions

The `cgc_` prefixed functions (`cgc_allocate`, `cgc_deallocate`, `cgc_receive`, `cgc_transmit`) are not in DTT's annotation files. DTT still finds issues through:
- **Null pointer analysis**: Detects unguarded pointer use regardless of allocation source
- **Bounds checking**: Detects array index violations regardless of data source
- **Uninitialized analysis**: Detects use-before-init regardless of context

Adding `cgc_` annotations would improve taint tracking (marking `cgc_receive` as a taint source) and resource tracking (marking `cgc_allocate`/`cgc_deallocate` as malloc/free equivalents).

### 2. Multi-file Challenges

Some challenges have source split across many files. DTT processes each translation unit independently, so cross-file data flow isn't tracked.

### 3. No Ground Truth Comparison

This evaluation measures "does DTT find something" per challenge, not "does DTT find the specific injected vulnerability." A per-CWE ground truth comparison would require matching DTT findings against each challenge's README-documented CWEs, which is future work.

## Comparison with Competition Results

During the 2016 CGC competition, the winning system (Mayhem) found vulnerabilities in ~67% of challenges using dynamic analysis (fuzzing + symbolic execution). DTT achieves 87% detection using purely static analysis — though the metrics aren't directly comparable since:
- DTT reports any safety violation (including null checks), not just exploitable bugs
- The competition required working exploits, not just vulnerability detection
- DTT runs in seconds per challenge; competition systems had minutes

## Recommendations

1. **Add `cgc_` annotations**: `cgc_receive` as taint source, `cgc_allocate`/`cgc_deallocate` as resource tracking would enable taint flow analysis
2. **Per-CWE ground truth**: Match DTT findings against challenge READMEs to measure true positive vs false positive rates per CWE type
3. **False positive analysis**: Sample "clean" challenges and verify DTT findings are genuine issues
