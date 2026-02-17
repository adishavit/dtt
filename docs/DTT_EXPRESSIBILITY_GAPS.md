# DTT Expressibility Gaps

Analysis of what 10 real-world C/C++ libraries need from DTT annotations
that the current `.dtt` format and/or engine cannot express.

Derived from annotating: libwebrtc, libusb, libssh2, s2n-tls,
libarchive, libpng, zlib, libdbus, FFmpeg, libuv.

---

## Gap 1: `.dtt` Parser Lacks `||` / `&&` Support

**Status:** Parser gap only — engine fully supports `LogicalOr`, `LogicalAnd`, `LogicalNot`

**Impact:** HIGH — affects every library with multi-state machines

**What it blocks:**

libwebrtc's signaling state machine requires disjunctive preconditions:
```
# Cannot write this in .dtt today:
SetRemoteDescription(1):
  requires @signaling_state == 1 || @signaling_state == 4
```

libssh2's authentication accepts handshake OR authenticated state:
```
# Cannot write this today:
libssh2_session_hostkey:
  requires arg0.@session_state == 2 || arg0.@session_state == 3
```

**Workaround:** Use `!=` to guard against the most dangerous single state
(e.g., `requires @signaling_state != 5` instead of enumerating valid states).

**Fix:** Add `parseLogicalAnd()` / `parseLogicalOr()` to `DTTAnnotationFile.cpp`,
mirroring the pattern at `DTTPlugin.cpp:2039-2058`.

---

## Gap 2: Conditional Postconditions (Result-Dependent)

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

## Gap 3: Cross-Object Constraints (Ownership Hierarchies)

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

## Gap 4: Conditional Preconditions (State-Dependent Rules)

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

## Gap 5: Indirect Out-Parameters

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

## Gap 6: Named State Enumerations

**Status:** Syntactic gap — integers work but are error-prone

**Impact:** LOW (functional) / HIGH (readability)

**What it blocks:**

```
# Current: magic numbers
requires @signaling_state != 5
ensures @dc_ready_state = 2

# Desired: named constants
enum SignalingState { STABLE=0, HAVE_LOCAL_OFFER=1, ..., CLOSED=5 }
requires @signaling_state != SignalingState.CLOSED
ensures @dc_ready_state = DataChannelState.CLOSING
```

**Libraries affected:** All (every library uses multi-valued state machines)

**Workaround:** Use integer constants with comments. This is functional but
makes `.dtt` files harder to read and more prone to typos.

**Proposed syntax:**
```
enum SignalingState {
  STABLE = 0
  HAVE_LOCAL_OFFER = 1
  HAVE_REMOTE_OFFER = 2
  CLOSED = 5
}
```

---

## Gap 7: Ownership Transfer / Consumption

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

## Gap 8: Argument Value Bounds Constraints

**Status:** Partially representable (only via `requires arg0 < _size`)

**Impact:** LOW-MEDIUM — affects buffer APIs

**What it blocks:**

```
# libpng: CVE-2026-25646 — png_set_quantize max_colors must be <= 256
fn png_set_quantize(4) {
  requires arg2 <= 256    # can't express: arg value bounded by constant
}

# zlib: window bits must be in range [8, 15]
fn deflateInit2(6) {
  requires arg3 >= 8
  requires arg3 <= 15
}
```

Currently, `.dtt` `requires` for free functions only supports
`argN.FIELD OP VALUE`, not `argN OP VALUE` (raw argument constraints).

**Workaround:** These can only be caught by the library internally or by
DTT's arithmetic expression tracking when the value comes from a known path.

**Proposed syntax:**
```
requires arg2 <= 256 "max_colors exceeds palette size"
requires arg3 >= 8 && arg3 <= 15 "invalid window bits"
```

---

## Gap 9: Scope-Exit on Arguments (Not Just Results)

**Status:** Partially representable

**Impact:** LOW — most acquire functions return the resource

**What it blocks:**

```
# zlib: deflateInit takes a z_stream* and initializes it
# The z_stream must have deflateEnd called before scope exit
fn deflateInit(2) {
  ensures arg0.@stream_state = 1
  scope_exit arg0.@stream_state == 0    # Works in current .dtt!
}
```

Actually, looking at the `.dtt` format more carefully, `scope_exit` currently
only supports `result.FIELD OP VALUE`. The zlib annotations need
`scope_exit arg0.@stream_state == 0`.

**Libraries affected:** zlib, libarchive (entry), libuv (handles initialized
via out-param)

**Workaround:** If the init function returns the object, use
`ensures result.@field` + `scope_exit result.@field`. For functions that
initialize via argument, the leak detector may not fire.

**Fix:** Extend `parseFnScopeExit` to accept `argN.FIELD` in addition to
`result.FIELD`.

---

## Gap 10: Thread Affinity Tracking

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
| 1. `\|\|`/`&&` in `.dtt` parser | HIGH | EASY (copy from plugin parser) | All 10 |
| 2. Conditional postconditions | HIGH | MEDIUM (new syntax + engine) | All 10 |
| 3. Cross-object constraints | MEDIUM | HARD (identity tracking) | 6 of 10 |
| 4. Conditional preconditions | MEDIUM | MEDIUM (new syntax) | 3 of 10 |
| 5. Indirect out-parameters | MEDIUM | MEDIUM (deref tracking) | 4 of 10 |
| 6. Named enumerations | LOW/HIGH | EASY (parser only) | All 10 |
| 7. Ownership transfer | MEDIUM | MEDIUM (consume semantics) | 3 of 10 |
| 8. Arg value bounds | LOW-MEDIUM | EASY (extend parser) | 3 of 10 |
| 9. Scope-exit on args | LOW | EASY (extend parser) | 3 of 10 |
| 10. Thread affinity | LOW | HARD (conceptual) | 3 of 10 |

### Recommended Fix Order

1. **Gap 1** (parser `||`/`&&`) — trivial, unblocks all state machines
2. **Gap 9** (scope-exit on args) — trivial parser extension
3. **Gap 6** (named enums) — parser-only, massive readability win
4. **Gap 8** (arg value bounds) — easy parser extension
5. **Gap 2** (conditional postconditions) — most impactful engine change
6. **Gap 5** (indirect out-params) — common C pattern
7. **Gap 4** (conditional preconditions) — enables shared/private distinction
8. **Gap 7** (ownership transfer) — sugar over ghost fields
9. **Gap 3** (cross-object constraints) — fundamental extension
10. **Gap 10** (thread affinity) — research territory
