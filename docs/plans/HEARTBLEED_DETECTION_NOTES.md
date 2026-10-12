# Heartbleed Detection Notes

Working notes from an investigation into whether DTT (and Meta's Infer) catch
the real Heartbleed bug (CVE-2014-0160), what is missing, and the smallest
general change that closes the gap. Experiments used reductions of OpenSSL
1.0.1f (vulnerable) and 1.0.1g (fixed) `tls1_process_heartbeat()`.

Status markers: **verified** = reproduced by running the tool;
**inferred** = reasoning or documentation, not reproduced.

## 1. The bug in one paragraph

`tls1_process_heartbeat()` reads a 16-bit `payload` length from the record and
copies `payload` bytes from `pl` (a pointer into `s->s3->rrec.data`) without
checking it against `s->s3->rrec.length`. The fix adds two early returns:

```c
if (1 + 2 + 16 > s->s3->rrec.length) return 0;
...
if (1 + 2 + payload + 16 > s->s3->rrec.length) return 0;
```

It is a source over-read (CWE-126) of a buffer that is allocated and sized
elsewhere (the record layer), with its length in a sibling struct field.

## 2. What existing tools do

| Tool / mode | Vulnerable | Fixed | Status |
|---|---|---|---|
| Infer v1.3.0 default checkers (Pulse) | Not reported (only unchecked `malloc` null deref) | Same | verified |
| Infer `--bufferoverrun` (deprecated InferBO), default filtering | Not reported | Not reported | verified |
| InferBO `--no-filtering` | `BUFFER_OVERRUN_L5` at the copy (lowest confidence) | Not reported | verified |
| DTT inform / enforce / quiet | Not reported | Not reported | verified |

Why Pulse misses it (inferred from its documentation): Pulse has no buffer-bounds
issue type, and it reports only bugs that occur for all inputs ("manifest").
An input-dependent over-read stays "latent".

Why DTT misses it (verified): DTT knows a pointer's extent only when it sees the
allocation in the same function. `885_heartbleed_overread_fail.cpp` uses a local
array, so it is caught. `rrec.data` is a struct field allocated elsewhere, so its
extent is unknown and the `memcpy` source check is skipped, even in enforce mode.
Known-gap fixture: `895_heartbleed_real_shape_xfail.cpp`.

## 3. Root causes found in DTT

1. **No extent for pointer fields.** Nothing relates `rrec.data` to
   `rrec.length`. The relation is established in another translation unit, so a
   single-TU analysis cannot infer it without a contract. A function-level
   `dtt:requires=data._size_bytes >= length` is not assumed inside the body.
2. **No relational path conditions.** Guards compare a variable with a constant,
   or a variable with an object field/method (`VarBoundProofs`). A comparison of
   two plain scalars (`n <= len`) or of expressions (`payload + 19 > length`) is
   not recorded. `VarBoundProofs` are also not passed to the `memcpy` solver
   query (`checkComparisonWithZ3` calls `addEnvironmentConstraints()` without
   `includeVarBounds`).
3. **`&E[i]` treated as an element access.** `&s->s3->rrec.data[0]` is checked as
   a read of index 0, which fails when the extent may be zero.
4. **Pointer derivation only from plain variables.** `p = q` copies `q`'s buffer
   properties; `p = s->field` and `p = &q[i]` (pointer `q`) do not.

## 4. Minimal general change (prototype)

A scratch prototype added four general features. On the unmodified 1.0.1f/1.0.1g
bodies, plus one annotation on `SSL3_RECORD::data`, the vulnerable `memcpy` gets
a new CWE-126 diagnostic and the fixed version gets no new bounds diagnostic.

| Part | Change | Without it (ablation, verified) |
|---|---|---|
| C1 | `dtt:counted_by=F` on a pointer field: extent is `parent.F` elements | Vulnerable not flagged |
| F2b | `p = &q[i]` on a tracked pointer: `p` inherits `q`'s extent at offset `i` | Vulnerable not flagged |
| F2a | `&E[i]` forms a pointer; valid for `i <= extent`; `&E[0]` always valid | Fixed gets a false bounds error |
| R | Relational path conditions over linear expressions, fed to Z3 | Fixed gets false bounds errors |

Necessity is shown by ablation (removing any one part fails the criterion);
sufficiency by the prototype. This proves the set is irreducible, not that no
smaller design exists.

A smaller detection-only alternative exists: report `memcpy` from an
unknown-extent source in enforce mode. It flags the vulnerable version without
any annotation, but also flags the fixed version and every such copy.

Not needed for Heartbleed but needed for a non-tailored feature: then/else
scoping of relational facts, `&&`/`||` conditions, `p = s->field`, and
`counted_by` lookup at direct use sites (`memcpy(d, s->data, n)`, `s->data[i]`).

## 5. Side findings

- **Vacuous pass test.** `888_symbolic_size_guarded_pass.cpp` claims its guards
  are proven. They are not (root cause 2); it passes only because `_pass` tests
  run in inform mode, where an unverified bound is a warning.
- **Vacuous "proofs" while probing.** A copy from `data + 3` looked proven; it was
  skipped because `data + 3` had no tracked extent. Always confirm a solver query
  ran before trusting a pass.
- **Test asserting a false positive.** `144_nullptr_complex_sources_fail.cpp`
  marks three lines whose diagnostics its own comments call wrong ("XFAIL").
  Propagating properties through `p = c.ptr` removes them.
- **Remaining noise on the real OpenSSL code** (identical in both versions):
  - `s` / `s->s3` "nullability unknown" (parameters are never null-checked);
  - `s2n()` writes `bp[0]`, `bp[1]` flagged because the index solver query does
    not know unsigned values are non-negative.
- **Integer model.** DTT reasons over unbounded integers (no wraparound). The
  proof of the fix relies on that; under bit-precise semantics the `n2s` range
  (at most 65535) would also be needed.

## 6. Related branches and PRs (checked)

- PR #39 (taint tracking, open): flags a tainted copy length; does not check the
  source extent; a taint source must be called in the same function. Did not
  catch the real shape (verified on a build of the branch, by a sub-investigation).
- PR #29: binds a local to an already-tracked size; cannot supply an unknown extent.
- `add-cpp26-contracts-concepts`: `dtt_contract_assert(expr)` is an assume hook;
  cannot express `_size_bytes`.
- PR #1 (merged): `injectRequiresAsAssumption` is the natural place to make
  `requires=` on `_size_bytes` an in-body assumption (root cause 1, alternative).

## 7. Implementation status

See section 8 (updated as features land).

## 8. Implementation log

_Pending._
