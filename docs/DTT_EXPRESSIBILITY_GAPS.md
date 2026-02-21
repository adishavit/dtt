# DTT Expressibility Gaps

Analysis of what 10 real-world C/C++ libraries need from DTT annotations
that the current `.dtt` format and/or engine cannot express.

Derived from annotating: libwebrtc, libusb, libssh2, s2n-tls,
libarchive, libpng, zlib, libdbus, FFmpeg, libuv.

---

## Gap 1: Conditional Postconditions (Result-Dependent)

**Status:** Engine gap — not representable

**Impact:** HIGH — affects every function that returns NULL on failure

**What it blocks:**

Many C functions return NULL on failure. The postcondition depends on
whether the function succeeded:

```
# Cannot express today:
fn s2n_connection_new(1) {
  ensures_if_nonnull result.@conn_state = 1    # success path
  ensures_if_null result.@conn_state = 0        # failure path
}

fn sqlite3_prepare_v2(5) {
  ensures_if_success arg2.@stmt_state = 1       # SQLITE_OK
}

fn libssh2_channel_open_session(1) {
  ensures_if_nonnull result.@channel_state = 1  # success
  # failure returns NULL — no ghost to set
}
```

**Libraries affected:** ALL 10 (every library has fallible allocation)

**Workaround:** Always set the ghost field optimistically. DTT's `_nonnull`
tracking on the result pointer partially compensates — if the caller checks
for NULL, the ghost field is only accessed on the non-NULL branch.

**Proposed syntax:**
```
ensures_if_nonnull result.@state = 1
ensures_if_null result.@state = 0
ensures_if_return 0 result.@state = 1    # return value == 0 means success
ensures_if_return -1 result.@state = 0   # return value == -1 means failure
```

---

## Gap 2: Cross-Object Constraints (Ownership Hierarchies)

**Status:** Engine gap — not representable

**Impact:** MEDIUM — affects libraries with parent-child resource patterns

**What it blocks:**

```
# libssh2: session must outlive its channels
fn libssh2_channel_free(1) {
  # This channel's parent session must still be alive
  requires arg0.@parent_session.@session_state != 0
}

# s2n-tls: config must outlive connections using it
fn s2n_config_free(1) {
  requires ALL_CONNECTIONS_USING(arg0).@conn_state == 0
}

# libuv: all handles must be closed before loop closes
fn uv_loop_close(1) {
  requires ALL_HANDLES_ON(arg0).@handle_state == 4
}

# LevelDB: iterators must be destroyed before database
fn leveldb_iter_destroy(1) {
  requires arg0.@parent_db.@db_open == 1
}
```

**Libraries affected:** libssh2, s2n-tls, libuv, libwebrtc, libdbus, LevelDB

**Workaround:** Rely on scope-exit checks. If both parent and child are in
the same scope, DTT's `scope_exit` on the child ensures it's released before
the scope ends. This works for the common case but not for objects stored in
containers or passed across function boundaries.

**Proposed extension:** Ghost field aliasing:
```
fn libssh2_channel_open_session(1) {
  ensures result.@parent_session = arg0    # links channel to session
}
fn libssh2_channel_free(1) {
  requires arg0.@parent_session.@session_state != 0
}
```

---

## Gap 3: Conditional Preconditions (State-Dependent Rules)

**Status:** Engine gap — partially representable

**Impact:** MEDIUM — affects libdbus, libusb, and similar

**What it blocks:**

```
# libdbus: Only private connections can be closed
fn dbus_connection_close(1) {
  requires arg0.@conn_type == 2    # only if private
  # But actually: IF @conn_type == 1 (shared), close is FORBIDDEN
  # IF @conn_type == 2 (private), close is REQUIRED before unref
}

fn dbus_connection_unref(1) {
  # If private: requires @conn_state == 2 (must be closed first)
  # If shared: @conn_state can be 1 (just unref without close)
  requires_if arg0.@conn_type == 2 then arg0.@conn_state == 2
}
```

**Workaround:** Document the rules in comments. Use the simpler precondition
that catches the most dangerous case (e.g., `requires arg0.@conn_state != 0`
catches use-after-free for both shared and private).

**Proposed syntax:**
```
requires_if arg0.@conn_type == 2 then arg0.@conn_state == 2
```

---

## Gap 4: Indirect Out-Parameters

**Status:** Engine gap — partially representable

**Impact:** MEDIUM — affects C APIs that return via pointer-to-pointer

**What it blocks:**

Many C functions return objects through out-parameters:
```
# libusb: device handle returned via pointer-to-pointer
fn libusb_open(2) {
  # arg1 is libusb_device_handle** — the HANDLE is at *arg1
  # Cannot set ghost field on *arg1 (the pointed-to handle)
  ensures (*arg1).@dev_open = 1     # not expressible
}

# SQLite: prepared statement returned via out-param
fn sqlite3_prepare_v2(5) {
  # arg2 is sqlite3_stmt** — statement handle at *arg2
  ensures (*arg2).@stmt_state = 1   # not expressible
}

# FFmpeg: format context via double pointer
fn avformat_open_input(4) {
  # arg0 is AVFormatContext** — context at *arg0
  ensures (*arg0).@format_state = 2  # not expressible
}
```

**Libraries affected:** libusb, SQLite, FFmpeg, libpng (destroy functions)

**Workaround:** For functions that return the resource as their return value
(e.g., `libssh2_session_init` returns `LIBSSH2_SESSION*`), use
`ensures result.@field`. For true out-parameters, the ghost field must be
set manually through the variable that receives the value.

**Proposed syntax:**
```
ensures *arg1.@dev_open = 1     # dereference out-parameter
ensures arg2->@stmt_state = 1   # alternative syntax
```

---

## Gap 5: Named State Enumerations

**Status:** FIXED — `enum` blocks supported in `.dtt` format

**Impact:** LOW (functional) / HIGH (readability)

**Syntax:**
```
enum SignalingState {
  STABLE = 0
  HAVE_LOCAL_OFFER = 1
  HAVE_REMOTE_OFFER = 2
  CLOSED = 5
}

# Use EnumName.MEMBER anywhere an integer is accepted:
requires @signaling_state != SignalingState.CLOSED
ensures @dc_ready_state = DataChannelState.CLOSING
scope_exit result.@state == FileState.CLOSED
```

Enum references resolve in: `requires`, `ensures`, `ensures_if_true/false`,
`scope_exit`, and free function `requires`/`ensures`/`scope_exit` directives.

---

## Gap 6: Ownership Transfer / Consumption

**Status:** Engine gap — not representable

**Impact:** MEDIUM — affects resource-passing APIs

**What it blocks:**

Some functions consume/invalidate an argument:
```
# After SSL_free, the SSL* is invalidated — any use is UB
fn SSL_free(1) {
  consumes arg0    # marks arg0 as invalidated after call
}

# After av_packet_move_ref, src packet is invalidated
fn av_packet_move_ref(2) {
  consumes arg1              # src is emptied
  ensures arg0.@packet_alive = 1  # dst receives ownership
}

# libusb transfer with FREE_TRANSFER flag:
# The library frees the transfer in the callback
fn libusb_submit_transfer(1) {
  # If transfer->flags & LIBUSB_TRANSFER_FREE_TRANSFER,
  # the transfer is freed in the callback — caller must not free
  conditionally_consumes arg0
}
```

**Libraries affected:** FFmpeg (move_ref), OpenSSL (SSL_free), libusb
(FREE_TRANSFER flag)

**Workaround:** Use ghost fields to model: `ensures arg0.@alive = 0` on
the consuming function. The caller then triggers an error if they try to
use the object after (since `@alive == 0` violates preconditions).

---

## Gap 7: Argument Value Bounds Constraints

**Status:** FIXED — `requires argN OP VALUE` supported (raw arg constraints)

**Impact:** LOW-MEDIUM — affects buffer APIs

**Syntax:**

```
# libpng: png_set_quantize max_colors must be <= 256
fn png_set_quantize(4) {
  requires arg2 <= 256
}

# zlib: window bits must be in range [8, 15]
fn deflateInit2(6) {
  requires arg3 >= 8
  requires arg3 <= 15
}
```

Parser sets `isRawArg = true` when no `.FIELD` suffix is present,
and the engine checks the raw argument value directly.

---

## Gap 8: Scope-Exit on Arguments (Not Just Results)

**Status:** FIXED — `scope_exit argN.FIELD OP VALUE` supported

**Impact:** LOW — most acquire functions return the resource

**Syntax:**

```
# zlib: deflateInit takes a z_stream* and initializes it
# The z_stream must have deflateEnd called before scope exit
fn deflateInit(2) {
  ensures arg0.@stream_state = 1
  scope_exit arg0.@stream_state == 0
}
```

`parseFnScopeExit` supports both `result.FIELD` and `argN.FIELD` modes.
Actively used in zlib.dtt and other library annotations.

---

## Gap 9: Thread Affinity Tracking

**Status:** Engine gap — not representable

**Impact:** LOW (compile-time) / HIGH (conceptual)

**What it blocks:**

```
# libwebrtc: methods must be called from signaling thread
PeerConnection::Close():
  requires current_thread == signaling_thread

# libuv: handle callbacks fire on the loop thread
uv_async_send():
  # Can be called from any thread (exception to the rule)
  thread_safe

# s2n-tls: after handshake, send/recv can be on separate threads
# but not multiple threads calling the same function
s2n_send():
  requires exclusive_access(arg0, write)
```

Thread affinity is fundamentally a runtime property — the "current thread"
is not a compile-time constant. However, DTT could potentially track it
as a ghost field set by annotation (e.g., marking a block as "running on
signaling thread") and checking preconditions against it.

**Workaround:** None at compile time. Runtime tools like TSan or
`RTC_DCHECK_RUN_ON` are the current solution.

---

## Summary: Priority Matrix

| Gap | Severity | Fix Difficulty | Libraries Affected |
|-----|----------|----------------|-------------------|
| 1. Conditional postconditions | HIGH | MEDIUM (new syntax + engine) | All 10 |
| 2. Cross-object constraints | MEDIUM | HARD (identity tracking) | 6 of 10 |
| 3. Conditional preconditions | MEDIUM | MEDIUM (new syntax) | 3 of 10 |
| 4. Indirect out-parameters | MEDIUM | MEDIUM (deref tracking) | 4 of 10 |
| 5. Named enumerations | ~~LOW/HIGH~~ | ~~EASY~~ **FIXED** | All 10 |
| 6. Ownership transfer | MEDIUM | MEDIUM (consume semantics) | 3 of 10 |
| 7. Arg value bounds | ~~LOW-MEDIUM~~ | ~~EASY~~ **FIXED** | 3 of 10 |
| 8. Scope-exit on args | ~~LOW~~ | ~~EASY~~ **FIXED** | 3 of 10 |
| 9. Thread affinity | LOW | HARD (conceptual) | 3 of 10 |

### Recommended Fix Order

1. ~~**Gap 8** (scope-exit on args)~~ — **FIXED**
2. ~~**Gap 5** (named enums)~~ — **FIXED**
3. ~~**Gap 7** (arg value bounds)~~ — **FIXED**
4. **Gap 6** (ownership transfer) — easy, reuses invalidation
5. **Gap 3** (conditional preconditions) — easy, reuses evaluateConstraintBool
6. **Gap 1** (conditional postconditions) — highest impact, extends method pattern
7. **Gap 4** (indirect out-params) — common C pattern, pointer infra exists
8. **Gap 2** (cross-object constraints) — new concept (ghost-field-as-reference)
9. **Gap 9** (thread affinity) — research territory, questionable static feasibility

---

## Implementation Plan

Grounded analysis of each remaining gap against actual parser (`DTTAnnotationFile.cpp`)
and engine (`DTTPlugin.cpp`) code.

### Tier 1 — Low-Hanging Fruit (~380 LOC combined)

#### Gap 6: Ownership Transfer (`consumes arg0`) — ~130 LOC

| Component | LOC | Details |
|-----------|-----|---------|
| Parser | ~20 | New `consumes` keyword in `parseFnBlock()` |
| Engine | ~100 | Call existing `invalidatePropertiesWithProvenance()` after postconditions (line ~14742) |
| Types | ~10 | Add `SmallVector<int, 2> consumedArgs` to `FreeFunctionInfo` |

**Reuses:** `invalidatePropertiesWithProvenance()` (line 3300), argument path resolution.
**Enables:** FFmpeg `av_packet_move_ref`, OpenSSL `SSL_free`, libusb `FREE_TRANSFER`.
**Risk:** Low — targeted invalidation on a known arg index.

#### Gap 3: Conditional Preconditions (`requires_if GUARD then BODY`) — ~250 LOC

| Component | LOC | Details |
|-----------|-----|---------|
| Parser | ~80 | New `requires_if` directive; parse guard + body as two `ConstraintExprPtr` |
| Engine | ~150 | Evaluate guard via existing `evaluateConstraintBool()` (line 3106); check body only if guard true |
| Types | ~20 | New `ConditionalPrecondition { guard, body }` struct |

**Reuses:** `evaluateConstraintBool()`, `checkPreconditions` path (line ~11850).
**Enables:** libdbus shared/private connection distinction, libusb mode-dependent rules.
**Risk:** Low — guard is just another constraint expression.

### Tier 2 — Medium Effort, High Impact (~560 LOC combined)

#### Gap 1: Conditional Postconditions (`ensures_if_nonnull`) — ~400 LOC

| Component | LOC | Details |
|-----------|-----|---------|
| Parser | ~150 | Extend free function `ensures` with guard variants: `ensures_if_nonnull`, `ensures_if_return N` |
| Engine | ~200 | Extend `applyPostconditions()` (line ~7208) with guard evaluation; reuse `inferNonnullFromExpr()` (line ~13845) for nonnull check |
| Types | ~50 | Add guard type enum to `FreeFunctionPostcondition` |

**Reuses:** `BoolPostcondition` pattern from methods (`ensures_if_true`/`ensures_if_false`), `inferNonnullFromExpr()`.
**Enables:** Every library with fallible allocation (all 10). Highest real-world impact.
**Risk:** Low — proven pattern from method annotations.

#### Gap 4: Indirect Out-Parameters (`ensures *arg1.@field`) — ~160 LOC

| Component | LOC | Details |
|-----------|-----|---------|
| Parser | ~30 | Recognize leading `*` in `parseFnEnsures()` (line 455), set deref flag |
| Engine | ~120 | Resolve pointer target via `PointerAliases` (line 2263), apply postcondition to pointee |
| Types | ~10 | Add `bool isDeref` to `FreeFunctionPostcondition` |

**Reuses:** `PathElement::Deref` factory (line 168, exists but unused), `PointerAliases` map.
**Enables:** libusb `libusb_open(&handle)`, FFmpeg `avformat_open_input`, SQLite `sqlite3_prepare_v2`.
**Risk:** Medium — pointer may be null or ambiguous at call site.

### Tier 3 — Architectural (~450 LOC)

#### Gap 2: Cross-Object Constraints (`arg0.@parent.@state`) — ~450 LOC

| Component | LOC | Details |
|-----------|-----|---------|
| Parser | ~100 | Extend `parseConstraintAtom()` (line 134) to chain field lookups: `argN.@field1.@field2` |
| Engine | ~300 | Ghost-field-as-reference: when `@parent` holds an object ref, follow the chain to resolve `@state` on the target object |
| Types | ~50 | Extend `ArgFieldRefExpr` to support field chains |

**New concept:** Ghost field values can reference other tracked objects (alias map).
**Enables:** libssh2 session/channel lifetime, libuv loop/handle ownership, s2n-tls config/connection.
**Risk:** Medium — circular references, dangling aliases.

### Tier 4 — Research (skip for now)

#### Gap 9: Thread Affinity — ~280 LOC but questionable feasibility

Static analysis cannot verify runtime thread identity. Would require "trust me" annotations
that amount to assertions rather than proofs. Defer to runtime tools (TSan, `RTC_DCHECK_RUN_ON`).

### Summary

| Tier | Gaps | Combined LOC | Sessions |
|------|------|-------------|----------|
| 1 | Gap 6 + Gap 3 | ~380 | 1 |
| 2 | Gap 1 + Gap 4 | ~560 | 1-2 |
| 3 | Gap 2 | ~450 | 1 |
| 4 | Gap 9 | Skip | — |
| **Total** | **5 gaps** | **~1,390** | **3-4** |
