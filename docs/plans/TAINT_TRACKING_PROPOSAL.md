# DTT Taint Tracking Proposal

## Executive Summary

Add taint tracking to DTT to distinguish untrusted external data from internal values,
enabling more precise vulnerability detection and feeding dtx exploit chain analysis.

## Problem Statement

Current DTT warns on ALL unverified symbolic sizes:

```cpp
void internal_copy(size_t len) {
    char* buf = malloc(len);
    memcpy(buf, "data", len);  // WARNING: unverified (false positive?)
}

void heartbleed(int fd) {
    size_t len;
    recv(fd, &len, sizeof(len), 0);
    char* buf = malloc(len);
    memcpy(buf, record, len);  // WARNING: unverified (TRUE POSITIVE!)
}
```

Both produce the same warning, but the second is far more dangerous.

## Goals

1. Distinguish untrusted (tainted) values from internal values
2. Provide stronger warnings/errors for tainted unverified bounds
3. Reduce false positives on internal code
4. Detect taint flowing into dangerous sinks (command injection, format strings)
5. Maintain DTT's compile-time integration model
6. Produce taint metadata consumable by dtx (see `EXPLOIT_SCANNER_PLAN.md`)

## Non-Goals

1. Full path-sensitive analysis (too expensive for a plugin)
2. Inter-procedural taint tracking (Phase 1; dtx handles cross-function)
3. Replacing Clang Static Analyzer
4. Taint tracking through complex aliasing chains

## Known Limitations

Documented upfront to set expectations:

1. **Aliasing**: If `*ptr` is tainted, `ptr[1]` may not be tracked as tainted
   if accessed via a different path
2. **Interprocedural**: Taint doesn't flow across function boundaries without
   annotations (dtx adds this in its cross-TU pass)
3. **Containers**: Taint doesn't flow through `std::vector::push_back` etc.
4. **Struct member independence**: If `s.data` is tainted, `s.len` is not —
   each field is tracked independently

## Recommended Approach: Option D (Curated Rules)

Manually curate taint rules, seeded from CSA's GenericTaintChecker but
owned by DTT. CSA is the starting point, not the authority.

### Rationale

1. **Maintainability**: Explicit rules are easier to audit and modify
2. **Simplicity**: No complex build dependencies on CSA internals
3. **DTT-specific**: Rules tuned for DTT's needs (e.g., strlen taints length)
4. **C++ coverage**: CSA is C-centric; DTT must cover C++ idioms
5. **Documented**: Clear provenance, deviations from CSA noted

### Design Options Considered

| Option | Approach | Rejected because |
|--------|----------|-----------------|
| A | Copy CSA rules verbatim | Duplicates maintenance, no C++ coverage |
| B | Link against CSA libraries | Heavy dependency, different state model |
| C | Code-gen from CSA at build time | Fragile, complex build system |
| **D** | **Curated rules, seeded from CSA** | **(Selected)** |

---

## Implementation Status

| Phase | Status | Notes |
|-------|--------|-------|
| **Phase 1: Core Infrastructure** | **Done** | TaintEnv, `.dtt` parser, source/sink/propagator engine |
| **Phase 1: Rule Catalog** | **Done** | 31 sources, 88 propagators, 20 sinks in `libc.dtt`, cross-verified against CSA LLVM 18 |
| **Phase 1b: Z3 Taint Guards** | **Done** | Sound Z3-backed guard analysis replaces heuristic attenuation; handles `&&`, `\|\|`, `!`, `==`, `!=`; rejects tainted-vs-tainted bounds; type-range constraints for unsigned; `sizeof`/ternary/`CallExpr` support in Z3 expressions |
| **Phase 1c: Cast Propagation** | **Done** | Taint survives C-style casts (`(unsigned int)atoi(env)`) and `static_cast`; separate taint-only fallback in `VisitVarDecl` |
| **Phase 1d: argv Tainting** | **Done** | `main(argc, argv)` auto-taint; Medium risk; propagates through array subscripts and function calls |
| **Phase 1e: Aggregate Taint** | **Done** | Arrays with tainted element initializers detected at sinks; `isExprTainted()` checks `InitListExpr` recursively |
| **Phase 1f: Overflow-Sound Guards** | **Done** | `arithmeticTainted` flag rejects guards on values from tainted arithmetic (Mul/Add/Sub/Shl); prevents integer wrapping from bypassing bounds checks |
| Phase 3: C++ containers | Not started | `std::string`, `std::vector` taint propagation |
| Phase 4: Cross-TU serialization | Not started | Taint metadata in SARIF/module interface |

Tests: `1800_taint_source_fail`, `1801_taint_propagation_fail`, `1802_taint_guarded_pass`, `1803_taint_sanitizer_pass`, `1805_taint_heartbleed_fix_pass`, `1808_taint_z3_guard_patterns_pass`, `1809_taint_z3_tainted_bound_fail`, `1810_taint_cast_propagation_fail`, `1811_taint_argv_fail`, `1825_taint_validation_fail`, `1826_taint_validation_pass`, `1827_taint_aggregate_array_fail`, `1828_taint_aggregate_array_pass`, `1829_taint_overflow_guard_fail`, `1830_taint_overflow_guard_pass`.

---

## Implementation Architecture

All taint implementation is in `src/DTTPlugin.cpp`. Key components:

### Data Structures

- **`TaintInfo`**: Stores `SourceLocation origin`, `SmallString<16> sourceFn`, `RiskLevel` (High/Medium/Low), and `arithmeticTainted` flag (overflow-susceptible)
- **`TaintedPaths`**: `DenseMap<FieldPath, TaintInfo>` — the taint environment
- **`PendingTaintedReturn`**: `std::optional<TaintInfo>` — set by source rules on return values, consumed by `VisitVarDecl`/`VisitBinaryOperator`

### Taint Flow

1. **Sources**: `handleTaintInCall` matches function name against source/propagator/sink tables (populated from `libc.dtt` taint rules)
2. **Propagation**: Assignment propagation in `VisitVarDecl`/`VisitBinaryOperator`; arithmetic propagation for `+`, `-`, `*`, `/`, `%`, `<<`, `>>`; cast propagation via `IgnoreParenCasts` fallback
3. **Sinks**: Two categories — CONTENT sinks (CWE-78, CWE-22, CWE-134: command injection, path traversal, format strings) and SIZE sinks (CWE-190, CWE-120: allocation sizes, buffer operation lengths)
4. **Guard analysis**: Z3 SMT solver verifies guard conditions produce actual upper bounds on tainted variables; rejects tainted-vs-tainted bounds; only suppresses at SIZE sinks

### Cast Propagation Architecture

`VisitVarDecl` has two unwrapping paths for finding `CallExpr` in initializers:
1. **Main path**: `IgnoreParenImpCasts` — finds calls for postcondition application
2. **Taint fallback**: `IgnoreParenCasts` — finds calls through explicit casts (e.g., `(unsigned int)atoi(env)`) for taint-only purposes

### Z3 Taint Guard System

When a tainted variable reaches a SIZE sink, DTT checks guard conditions:
1. `clangCondToZ3` converts the AST guard to Z3 expressions
2. Unsigned type-range constraints added (e.g., `uint32 ∈ [0, 4294967295]`)
3. Checks satisfiability of `¬(tainted_var ≤ bound)` under guard constraints
4. `hasNonTaintedBoundInCond` rejects guards where both sides are tainted
5. Suppressed warnings emit: `remark: DTT: taint suppressed at size sink: value bounded by '<expr>'`

---

## Rule Catalog

Rules are seeded from CSA's GenericTaintChecker (LLVM 18) but owned by DTT.
Deviations from CSA are marked. DTT-specific additions are marked.

The canonical source for these rules is `annotations/libc.dtt`. The tables
below document the design rationale for each rule category.

### Taint Sources (31 rules)

| Category | Functions | Risk | Notes |
|----------|-----------|------|-------|
| stdin/char input | `getchar`, `getchar_unlocked`, `getch`, `wgetch`, `_IO_getc` | High | |
| Line input | `gets`, `gets_s` | High | Taints buffer and return |
| Formatted input | `scanf`, `scanf_s`, `vscanf` | High | Taints all variadic output args |
| Environment | `getenv`, `secure_getenv` | Medium | |
| System info | `getcwd`, `getwd`, `get_current_dir_name`, `gethostname`, `getnameinfo`, `getlogin`, `getlogin_r`, `getgroups`, `ttyname`, `ttyname_r` | Low | |
| Symlinks | `readlink`, `readlinkat` | Low | |
| File I/O | `fopen`, `fdopen`, `freopen`, `popen` | Low | FILE* as propagation vehicle |
| Network I/O | `recv`, `recvfrom`, `recvmsg`, `recvmmsg` | High | **[DTT-SPECIFIC]** treated as sources, not propagators — see Deviations from CSA |

### Taint Propagators (88 rules)

| Category | Functions | Notes |
|----------|-----------|-------|
| File/fd I/O | `read`, `pread`, `readv`, `preadv`, `fread`, `fgetc`, `fgets`, `fgetln`, `fgetws`, `getc`, `getc_unlocked`, `getw`, `getline`, `getdelim`, `fscanf`, `fscanf_s`, `sscanf`, `sscanf_s`, `vfscanf`, `vsscanf` | Tainted fd/FILE* taints buffer |
| Socket | `accept` | **[DTT-ADDITION]** |
| Memory copy | `memcpy`, `memmove`, `mempcpy`, `bcopy`, `wmemcpy`, `wmemmove` | Tainted src → tainted dst |
| String copy | `strcpy`, `strncpy`, `stpcpy`, `stpncpy`, `strlcpy`, `wcscpy`, `wcsncpy` | |
| String concat | `strcat`, `strncat`, `strlcat`, `wcscat`, `wcsncat` | |
| String dup | `strdup`, `strndup`, `strdupa`, `strndupa`, `wcsdup` | |
| String search | `strstr`, `strcasestr`, `memchr`, `memrchr`, `rawmemchr`, `strchr`, `strrchr`, `strchrnul`, `index`, `rindex`, `strpbrk`, `memmem` | |
| String compare | `strcmp`, `strcasecmp`, `strncmp`, `strncasecmp`, `memcmp`, `strspn`, `strcspn` | |
| Formatted output | `sprintf`, `snprintf`, `vsprintf`, `vsnprintf`, `__sprintf_chk`, `__snprintf_chk` | Tainted args → tainted buffer |
| Numeric conversion | `strtol`, `strtoll`, `strtoul`, `strtoull`, `strtof`, `strtod`, `strtold`, `atoi`, `atol`, `atoll`, `atof` | |
| Char class/conv | `tolower`, `toupper`, `isalnum`..`isxdigit` (14 functions) | |
| Multibyte/wide | `mbtowc`, `wctomb`, `wcwidth` | |
| Path ops | `basename`, `dirname`, `fnmatch`, `realpath` | |
| Sorting | `qsort`, `qsort_r` | |
| Realloc | `realloc` | |
| Length ops | `strlen`, `wcslen`, `strnlen`, `wcsnlen` | **[DTT-SPECIFIC]** CSA does NOT propagate through strlen; DTT does (Heartbleed vector) |

### Taint Sinks (20 rules)

| CWE | Functions | Sink args | Message |
|-----|-----------|-----------|---------|
| CWE-78 | `system`, `popen`, `execl`, `execle`, `execlp`, `execv`, `execve`, `execvp`, `execvpe`, `fexecve` | arg0 | Command/path injection |
| CWE-426 | `dlopen` | arg0 | Library injection |
| CWE-134 | `printf`, `fprintf`, `sprintf`, `snprintf`, `syslog`, `setproctitle` | format arg | Format string attack |
| CWE-22 | `open`, `fopen`, `creat`, `openat` | path arg | Path traversal |
| CWE-190 | `malloc`, `calloc`, `realloc`, `alloca` | size arg(s) | Tainted allocation size |
| CWE-120 | `memcpy`, `memmove`, `memset`, `strncpy`, `strncat` | length arg | Tainted buffer operation length |

---

## Future Work

### argv Tainting

`main(int argc, char* argv[])` — both `argc` and `argv` are attacker-controlled.
Detect `main()` via `FunctionDecl::isMain()` and taint both parameters.

### C++ Stream Taint Sources

CSA covers C functions only. DTT should handle:
- `std::cin >> x` → x is tainted (High)
- `std::getline(cin, s)` → s is tainted (High)
- `std::ifstream` reads → tainted (Low)

### Custom Taint Annotations

Users can extend taint tracking for their own APIs:

```cpp
[[clang::annotate("dtt:taints=retval")]]
char* read_user_input();

[[clang::annotate("dtt:sanitizes=arg0")]]
size_t validate_length(size_t len);
```

### Cross-TU Taint Serialization

Taint metadata in SARIF `properties` bag for dtx consumption:
```json
{
  "properties": {
    "dtt:taint": {
      "sourceFunction": "recv",
      "riskLevel": "high"
    }
  }
}
```

---

## Deviations from CSA

DTT intentionally differs from Clang Static Analyzer in these cases:

| Rule | CSA behavior | DTT behavior | Rationale |
|------|-------------|-------------|-----------|
| `strlen(tainted)` | Not tainted (metadata) | Tainted | Tainted length is the Heartbleed attack vector |
| `recv()` | Propagator (requires tainted fd) | Source (always taints buffer) | Server fds come from accept()/params, not locally tainted |
| `argv` | Not a taint source | Tainted (Medium risk) | Attacker controls program arguments |
| C++ streams | Not covered | `std::cin >>` is High risk | DTT is a C++ tool |
| Sinks | Only system/exec/popen | Also malloc size, array index, format string, file path | Broader vulnerability coverage |
| Taint sanitizers | Not supported | User-defined via annotations | Real code uses validation functions |
| Guard analysis | Not applicable | Z3 SMT solver proves bounds on tainted vars | CSA uses path-sensitive simulation; DTT uses AST-level proof |

---

## Open Questions

1. **Interprocedural taint**: If a function receives a tainted parameter,
   should the caller know?
   - Phase 4: `[[clang::annotate("dtt:tainted")]]` on parameters
   - dtx handles this via cross-TU analysis

2. **Taint through pointers**: If `*ptr` is tainted, is `ptr[1]` tainted?
   - Conservative: yes, entire buffer is tainted when any element is

3. **Taint decay**: Should taint clear after N operations?
   - No. Explicit proof (guard or sanitizer) required.

4. **`fopen` dual role**: fopen appears in both Sources (returns tainted FILE*)
   and Sinks (tainted path argument). Is this a conflict?
   - No conflict. They check different arguments.

5. **recv as source vs propagator**: Why not keep recv as a propagator?
   - See "Deviations from CSA" section. Server fds come from `accept()` or
     function parameters, which aren't pre-tainted in single-function analysis.

---

## Maintenance Plan

### Rule Ownership

DTT owns its taint rules. CSA is a reference, not an authority.

1. **Seed**: Rules initially derived from CSA GenericTaintChecker (LLVM 18)
2. **Divergence**: DTT may add, remove, or modify rules as needed
3. **CSA check**: Review CSA changes on major LLVM releases for new functions
4. **Documentation**: Each rule has a comment noting its origin (CSA or DTT-specific)

### Adding New Rules

When adding a taint rule:
1. Determine category (source / propagator / sink / sanitizer)
2. Write a test case FIRST (TDD per CLAUDE.md)
3. Add the rule to `annotations/libc.dtt`
4. Mark as `[DTT-ADDITION]` if not from CSA
5. Run full test suite

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| False positives from aggressive taint | High | Start conservative; sanitizer annotations allow suppression |
| Missing C++ stream detection | Medium | Phased: C functions first, C++ streams later |
| Performance overhead from taint maps | Low | DenseMap is O(1); StringMap index for rule lookup |
| Complex path merging | Medium | Conservative merge (union of taint) follows existing TypedEnv pattern |
| Users don't know about sanitizer annotations | Medium | Emit fix-it hints in diagnostics |

---

## Success Criteria

1. Heartbleed-style tests (`recv` + `memcpy`) produce tainted warnings
2. Command injection tests (`scanf` + `system`) produce sink warnings
3. Internal code (no external input) produces regular warnings only (not tainted)
4. Guarded tainted code passes silently (Z3-proven bounds)
5. Cast propagation works through C-style and static_cast
6. `argv`-based tests produce tainted warnings
7. No regression in existing DTT tests
8. Taint metadata appears in SARIF output

---

## References

- Clang Static Analyzer GenericTaintChecker: `llvm-project/clang/lib/StaticAnalyzer/Checkers/GenericTaintChecker.cpp`
- DTT Safety Roadmap Part 9 (Taint Tracking): `docs/DTT_SAFETY_ROADMAP.md` — defines `_tainted`/`_validated` properties
- dtx Exploit Scanner Plan: `docs/plans/EXPLOIT_SCANNER_PLAN.md`
- CWE-78: OS Command Injection
- CWE-120: Buffer Copy without Checking Size of Input
- CWE-126: Buffer Over-read
- CWE-134: Use of Externally-Controlled Format String
- CWE-190: Integer Overflow
- CWE-22: Path Traversal
- CWE-20: Improper Input Validation
- CWE-426: Untrusted Search Path
- CERT C: STR31-C, ENV33-C, FIO30-C
