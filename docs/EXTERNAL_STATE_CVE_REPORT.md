# External State Tracking: CVE Analysis Report

## Executive Summary

DTT tracks real and synthetic fields as properties of objects — `v._size`, `p.@open`, `u._union_active`. But in real C/C++ code, an object's semantic validity is often determined by a *different* variable entirely. The state lives "outside" the object it describes.

This report catalogs **200 CVEs** where the root cause is exactly this gap: one variable's validity depends on another separate variable's value, and they went out of sync.

## The Pointer/Resource Precedent

DTT's `VarToResource` is already a form of external state tracking:

```
FILE* f  →  Resource(42)  →  { @open: 1 }
FILE* g = f  →  Resource(42)  →  { @open: 1 }
fclose(f)  →  Resource(42)  →  { @open: 0 }
use(g)  →  ERROR: resource closed
```

The pointer `f` doesn't "own" the `@open` state. It lives in a shared `HeapObjectState`, and multiple variables can reference it. The patterns below are all variations of this same fundamental structure — just not yet generalized in DTT.

## Taxonomy of External State Patterns

### Pattern 1: Buffer + Separate Length Variable

The pointer and its size live in different variables/fields. They desync.

Examples: `malloc(n)` + separate `size_t len = n`, protocol headers where a length field describes a payload buffer, `UNICODE_STRING.Buffer` + `Length` + `MaximumLength`.

| # | CVE | Software | What desynced |
|---|-----|----------|---------------|
| 1 | CVE-2014-0160 | OpenSSL (Heartbleed) | Heartbeat `payload_length` field trusted without checking against actual record length |
| 2 | CVE-2023-38545 | curl SOCKS5 | Hostname length stored in wrong variable during slow SOCKS5 handshake |
| 3 | CVE-2021-3156 | sudo (Baron Samedit) | Off-by-one: escaped chars not counted in `user_args` buffer size |
| 4 | CVE-2023-4863 | libwebp | `BuildHuffmanTable` allocates buffer too small for input; size calc desynced from actual data |
| 5 | CVE-2020-0796 | Windows SMBv3 | Integer overflow: header says 0x1100 bytes, alloc gets 0x63, memcpy copies 0x15f8f |
| 6 | CVE-2024-38063 | Windows IPv6 | Packet size underflow: 0 wraps to 0xFFFF, copies 65488 bytes into 30-byte buffer |
| 7 | CVE-2021-46143 | Expat XML | Integer overflow on `m_groupSize`; realloc with size=0 acts as free, stale pointer used |
| 8 | CVE-2019-11477 | Linux TCP (SACK Panic) | Integer overflow in `tcp_gso_segs` causes kernel panic |
| 9 | CVE-2020-1027 | Windows CSRSS | `UNICODE_STRING.Buffer+Length` validated but `MaximumLength` field not checked |
| 10 | CVE-2020-1020 | Windows GDI | Font validation uses `Length` instead of `MaximumLength` |
| 11 | CVE-2018-0500 | curl SMTP | Scratch buffer sized as `2*download_buffer` instead of `2*upload_buffer` |
| 12 | CVE-2020-16898 | Windows TCP/IP | "Bad Neighbor": advances buffer 8 bytes too few, length/offset desync |
| 13 | CVE-2021-39685 | Linux USB Gadget | No limit on `wLength`, allows 65kB read/write past buffer boundaries |
| 14 | CVE-2016-3955 | Linux USB/IP | `urb->actual_length` from network trusted as buffer length |
| 15 | CVE-2022-37434 | zlib | Heap overflow via large gzip header extra field; size not checked against alloc |
| 16 | CVE-2018-25032 | zlib | Memory corruption in deflate when distant match count exceeds buffer |
| 17 | CVE-2024-12084 | Rsync | Heap overflow: checksum length (`s2length`) not validated against buffer |
| 18 | CVE-2023-5869 | PostgreSQL | Missing overflow checks in array modification; computed size wraps, small alloc |
| 19 | CVE-2015-1472 | glibc | `realloc(newsize)` instead of `realloc(newsize * sizeof(CHAR_T))` |
| 20 | CVE-2023-6246 | glibc vsyslog | Heap overflow via `argv[0]` affecting separate buffer size calculation |
| 21 | CVE-2017-17426 | glibc malloc | Near-SIZE_MAX request returns undersized buffer (tcache) |
| 22 | CVE-2021-3712 | OpenSSL ASN1 | `ASN1_STRING` buffer length field not guaranteed to match NUL termination |
| 23 | CVE-2022-3786 | OpenSSL X.509 | Variable-length email address overflows fixed-size buffer in name constraint check |
| 24 | CVE-2025-0725 | libcurl gzip | Integer overflow in zlib buffer size calculation |
| 25 | CVE-2019-12255 | VxWorks IPnet | TCP receive window up to 64k overflow; length field desynced |
| 26 | CVE-2020-35198 | VxWorks | Integer overflow in memory block size calc: actual < requested |
| 27 | CVE-2022-23943 | Apache mod_sed | Buffer management error allows OOB write with large data |
| 28 | CVE-2019-2107 | Android HEVC | OOB write in `ihevcd_parse_pps`; missing bounds check on separate size |
| 29 | CVE-2017-0781 | Android Bluetooth | L2CAP heap overflow: allocation too small for packet |
| 30 | CVE-2020-12352 | Linux Bluetooth | L2CAP "BadChoice": packet length desynced from parsing |
| 31 | CVE-2020-12351 | Linux Bluetooth | L2CAP "BadKarma": buffer handling flaw in packet parser |
| 32 | CVE-2022-40514 | Qualcomm WLAN | Buffer copy without size check on CCKM IE in reassoc |
| 33 | CVE-2019-9500 | Broadcom WiFi | Heap overflow: `brcmf_wowl_nd_results` event frame exceeds buffer |
| 34 | CVE-2019-9501 | Broadcom WiFi | Heap overflow when vendor IE data > 32 bytes |
| 35 | CVE-2020-8625 | BIND DNS | 4-byte heap overflow in GSSAPI SPNEGO; buffer too small |
| 36 | CVE-2020-25682 | dnsmasq | Buffer overflow in `extract_name()`: missing length check with DNSSEC |
| 37 | CVE-2024-5564 | libndp | Buffer overflow: route information length field > buffer |
| 38 | CVE-2018-6789 | Exim | Buffer overflow in `base64d`: decoded size calc off by one |
| 39 | CVE-2017-8816 | curl NTLM | Integer overflow when username+password > 2GB on 32-bit |
| 40 | CVE-2022-2566 | FFmpeg | Integer overflow in `sc->sample_offsets_count`; `av_calloc` too small |
| 41 | CVE-2025-49847 | llama.cpp | `size_t` cast to `int32_t` bypasses length check; memcpy overflows |
| 42 | CVE-2019-9023 | PHP mbstring | Incorrect length in `expand_case_fold_string` causes heap overflow |
| 43 | CVE-2015-8126 | libpng | Buffer overflow: `png_set_PLTE` with small bit-depth |
| 44 | CVE-2017-13253 | Android DRM | `CryptoInfo`: no verification of copy size vs destination buffer |
| 45 | CVE-2025-30388 | Windows GDI | EMF+ OOB read/write: string/rect count desynced from buffer |

### Pattern 2: External Type Discriminant / Tag-Union Confusion

A tag/type/discriminant variable says "type A" but the data is actually "type B". This includes JIT type speculation (compiler maintains separate type assumption about an object), serialization/deserialization type mismatches, and explicit tagged union misuse.

| # | CVE | Software | What desynced |
|---|-----|----------|---------------|
| 46 | CVE-2025-13223 | Chrome V8 | JIT type confusion; map transition speculates wrong type (exploited in wild) |
| 47 | CVE-2025-13224 | Chrome V8 | Type confusion found by Google's Big Sleep AI |
| 48 | CVE-2025-10585 | Chrome V8 | JIT assumes wrong object type after optimization |
| 49 | CVE-2024-7971 | Chrome V8 | Type confusion allowing heap corruption |
| 50 | CVE-2024-7965 | Chrome V8 | Type confusion in Maglev compiler |
| 51 | CVE-2024-5274 | Chrome V8 | Type confusion pre-125.0.6422.112 |
| 52 | CVE-2024-5830 | Chrome V8 | Confusion between fast-mode and dictionary objects |
| 53 | CVE-2024-4947 | Chrome V8 | Maglev: incorrect `AccessInfo` for module namespace object |
| 54 | CVE-2024-2887 | Chrome V8 | WebAssembly type confusion (Pwn2Own 2024) |
| 55 | CVE-2024-0517 | Chrome V8 | Maglev OOB write compiling classes with parent classes |
| 56 | CVE-2023-4069 | Chrome V8 | Maglev: incomplete object initialization → wrong type |
| 57 | CVE-2023-3079 | Chrome V8 | Type confusion zero-day (exploited in wild) |
| 58 | CVE-2023-2033 | Chrome V8 | JIT type confusion zero-day |
| 59 | CVE-2022-4262 | Chrome V8 | Type confusion exploited in wild |
| 60 | CVE-2022-1096 | Chrome V8 | Type confusion actively exploited |
| 61 | CVE-2020-6418 | Chrome V8 | Type confusion in JIT (exploited in wild) |
| 62 | CVE-2020-16040 | Chrome V8 | Type confusion in optimized code |
| 63 | CVE-2019-17026 | Firefox IonMonkey | JIT type confusion via `Array.pop` (exploited in wild) |
| 64 | CVE-2019-11707 | Firefox IonMonkey | `Array.prototype.pop` type confusion (exploited in wild) |
| 65 | CVE-2019-9816 | Firefox IonMonkey | ObjectGroup type confusion |
| 66 | CVE-2019-9810 | Firefox IonMonkey | `MArraySlice` incorrect alias info (Pwn2Own 2019) |
| 67 | CVE-2019-9791 | Firefox IonMonkey | Type inference incorrect for constructors via OSR |
| 68 | CVE-2024-44308 | Safari JSC | Register corruption in speculative JIT (exploited in wild) |
| 69 | CVE-2024-23222 | Safari WebKit | Type confusion processing web content (CISA KEV) |
| 70 | CVE-2023-32439 | Safari WebKit | DFG: `SpecBoolean` vs `SpecBytecodeNumber` confusion |
| 71 | CVE-2023-23529 | Safari WebKit | Type confusion zero-day (exploited in wild) |
| 72 | CVE-2022-32792 | Safari JSC | B3ReduceStrength OOB write in FTL compiler |
| 73 | CVE-2020-9802 | Safari JSC | DFG JIT integer overflow → type confusion |
| 74 | CVE-2019-0567 | Edge Chakra | `NewScObjectNoCtor`/`InitProto` opcode type confusion |
| 75 | CVE-2018-0838 | Edge Chakra | JIT Array type conversion confusion |
| 76 | CVE-2022-41033 | Windows COM+ | `PROPVARIANT` union: discriminant says BSTR, data is other (exploited) |
| 77 | CVE-2021-26871 | Windows WalletService | `PROPVARIANT` accessed as BSTR without discriminant check |
| 78 | CVE-2022-21882 | Windows Win32k | Window object type confusion (exploited in wild) |
| 79 | CVE-2022-34918 | Linux nf_tables | `nft_set_elem_init`: `nft_data` tagged union type confusion → buffer overflow |
| 80 | CVE-2018-15981 | Adobe Flash | AVM with-scope pointer type confusion zero-day |
| 81 | CVE-2018-4878 | Adobe Flash | Type confusion used to spread ROKRAT malware |
| 82 | CVE-2023-20963 | Android | WorkSource parcel type confusion → privilege escalation |
| 83 | CVE-2021-0928 | Android | Parcel deserialization type mismatch |
| 84 | CVE-2021-42321 | Exchange | Deserialization type confusion |
| 85 | CVE-2019-18935 | Telerik UI | Deserialization type confusion → RCE |
| 86 | CVE-2025-69420 | OpenSSL | `ASN1_TYPE` union accessed without type validation in timestamp verify |
| 87 | CVE-2024-33663 | python-jose | JWT algorithm confusion (alg field says one, key is another) |

### Pattern 3: Return Value / errno Encodes State of Another Variable

Function return or global errno describes a *different* object's state; misread or stale.

This includes: three-valued returns misread as boolean, unchecked returns where the return encodes whether an argument is valid, and errno/GetLastError temporal state that goes stale.

| # | CVE | Software | What desynced |
|---|-----|----------|---------------|
| 88 | CVE-2008-5077 | OpenSSL | `EVP_VerifyFinal()` returns -1 on error, 0 on failure, 1 on success; callers treated non-zero as success |
| 89 | CVE-2009-0021 | NTP | Unchecked `EVP_VerifyFinal()` return → invalid signature accepted |
| 90 | CVE-2009-0025 | BIND DNS | Unchecked `EVP_VerifyFinal()` and `DSA_do_verify()` returns |
| 91 | CVE-2009-0046 | Sun GridEngine | Unchecked `EVP_VerifyFinal()` return |
| 92 | CVE-2009-0047 | Gale | Unchecked `EVP_VerifyFinal()` return |
| 93 | CVE-2009-0048 | OpenEvidence | Unchecked `EVP_VerifyFinal()` return |
| 94 | CVE-2009-0049 | Belgian eID | Unchecked `EVP_VerifyFinal()` return |
| 95 | CVE-2009-0050 | Lasso | Unchecked `EVP_VerifyFinal()` return |
| 96 | CVE-2009-0051 | ZXID | Unchecked `EVP_VerifyFinal()` return |
| 97 | CVE-2012-2122 | MySQL | `memcmp()` return cast to `my_bool` (char); 1/256 chance wrong value → auth bypass |
| 98 | CVE-2023-0466 | OpenSSL | `X509_VERIFY_PARAM_add0_policy()` return doesn't enable policy checking |
| 99 | CVE-2024-12797 | OpenSSL | Handshake succeeds despite RPK verification failure; return not checked |
| 100 | CVE-2025-40036 | Linux FastRPC | Unchecked `copy_to_user()` return → resource leak |
| 101 | CVE-2017-11176 | Linux mq_notify | Failed `fget()` with fd=-1 → use-after-free |
| 102 | CVE-2024-1086 | Linux nf_tables | Unchecked `nft_verdict_init()` return → use-after-free |
| 103 | CVE-2010-3435 | pam_xauth | Unchecked `setuid()`/`setgid()` returns → runs as root |
| 104 | CVE-2023-40303 | GNU inetutils | Unchecked `set*id()` returns in ftpd/rcp/rlogin/rsh |
| 105 | CVE-2013-4559 | lighttpd | Unchecked `setuid()`/`setgid()` returns |
| 106 | CVE-2017-6964 | dmcrypt | Unchecked `setuid()`/`setgid()` returns |
| 107 | CVE-2022-24492 | Windows RPC | Unchecked return values → RCE |
| 108 | CVE-2022-24528 | Windows RPC | Unchecked return values → RCE |
| 109 | CVE-2022-26809 | Windows RPC | Zero-click RCE from unchecked validation returns |
| 110 | CVE-2020-12049 | D-Bus | File descriptor count return not checked → fd leak |
| 111 | CVE-2024-21626 | runc | Leaked fd not tracked → container escape |
| 112 | CVE-2025-24531 | pam_pkcs11 | `PAM_IGNORE` return misinterpreted → auth bypass |
| 113 | CVE-2025-23013 | pam-u2f | `PAM_IGNORE` handling → 2FA bypass |
| 114 | CVE-2014-8151 | curl | TLS session ID reused between verified/unverified connections |
| 115 | CVE-2025-14847 | MongoDB zlib | Returns allocated size instead of decompressed length |
| 116 | CVE-2017-7546 | PostgreSQL | Empty password accepted (return not distinguished from success) |
| 117 | CVE-2024-48916 | Ceph RadosGW | JWT `alg: none` signature verification returns success |
| 118 | CVE-2019-14287 | sudo | `uid=-1` treated as root due to unsigned conversion of return |
| 119 | CVE-2021-41617 | OpenSSH | Uninitialized supplemental groups → privilege escalation |
| 120 | CVE-2016-2107 | OpenSSL | Padding oracle: AES-NI CBC MAC check return leaks info |
| 121 | CVE-2014-0224 | OpenSSL CCS | Weak keying material accepted; CCS state not validated |
| 122 | CVE-2023-0228 | OpenSSL | NULL deref from unchecked digest init return |
| 123 | CVE-2019-12900 | bzip2 | OOB write: selector count return not validated |
| 124 | CVE-2021-23017 | nginx | 1-byte overwrite in resolver: return length off-by-one |
| 125 | CVE-2022-2929 | ISC DHCP | Memory leak from unchecked label length return |

### Pattern 4: Parent/Child Lifetime Dependency

Object A's validity depends on Object B being alive. B is destroyed; A becomes dangling. This includes: DOM node parent freed while child reference held, iterator invalidation, arena/pool destruction, context/session dependency, and callback/closure dangling references.

| # | CVE | Software | Lifetime dependency broken |
|---|-----|----------|---------------------------|
| 126 | CVE-2025-0445 | Chrome V8 | Object freed, child reference still live |
| 127 | CVE-2024-4671 | Chrome Blink | Visuals component: rendering object freed, reference held (CVSS 9.8) |
| 128 | CVE-2024-1284 | Chrome Mojo | IPC parent object freed, child message handler still bound |
| 129 | CVE-2024-1060 | Chrome Canvas | Canvas context freed, reference via crafted HTML |
| 130 | CVE-2024-2885 | Chrome Dawn | WebGPU object freed, dependent still active |
| 131 | CVE-2025-5068 | Chrome Blink | Rendering engine parent freed |
| 132 | CVE-2025-43529 | Safari WebKit | DOM node freed, child reference exploited (in wild) |
| 133 | CVE-2023-42950 | Safari WebKit | Rendering object lifetime dependency broken |
| 134 | CVE-2024-9680 | Firefox | Animation timeline freed, callback still references it (in wild) |
| 135 | CVE-2025-49844 | Redis | Lua GC frees object while C code holds reference (CVSS 10.0, 13-year bug) |
| 136 | CVE-2024-0232 | SQLite | `jsonParseAddNodeArray()`: JSON node array reallocated, stale pointer |
| 137 | CVE-2024-4741 | OpenSSL | `SSL_free_buffers`: buffer freed while SSL object still references it |
| 138 | CVE-2024-6197 | libcurl | Stack UAF: parent stack frame returns, callback references stale data |
| 139 | CVE-2024-25062 | libxml2 | `xmlValidatePopElement`: DTD validation node freed, still referenced |
| 140 | CVE-2024-56171 | libxml2 | `xmlSchemaIDCFillNodeTables`: schema node freed during validation |
| 141 | CVE-2025-12863 | libxml2 | `xmlSetTreeDoc`: doc reassignment frees nodes still in use |
| 142 | CVE-2025-24855 | libxslt | XPath context node freed during transform |
| 143 | CVE-2025-7425 | libxslt | Heap UAF from corrupted memory management of namespace nodes |
| 144 | CVE-2024-47834 | GStreamer | Matroska demuxer: frame buffer freed, codec still references it |
| 145 | CVE-2024-31578 | FFmpeg | `av_hwframe_ctx_init`: hardware frame context freed, codec holds stale ref |
| 146 | CVE-2024-43047 | Qualcomm DSP | DMA buffer freed, DSP still processing it (in wild, spyware) |
| 147 | CVE-2024-23382 | Qualcomm GPU | DMA fence parent freed during GPU operation |
| 148 | CVE-2025-23280 | NVIDIA Linux | Driver object freed, dependent kernel reference |
| 149 | CVE-2025-23281 | NVIDIA Windows | GPU context freed via race, still in use |
| 150 | CVE-2025-48543 | Android ART | Runtime object GC'd while native code holds reference |
| 151 | CVE-2024-58241 | Linux Bluetooth | HCI core torn down, delayed work timer fires on freed object |
| 152 | CVE-2024-45434 | Bluetooth AVRCP | AVRCP service object freed, callback fires (affects vehicles) |
| 153 | CVE-2024-47810 | Foxit Reader | 3D page object freed, rendering still references it |
| 154 | CVE-2025-54257 | Adobe Acrobat | PDF structure freed, JavaScript handler still holds reference |
| 155 | CVE-2024-41869 | Adobe Acrobat | PDF object freed via crafted document, still accessed |
| 156 | CVE-2025-62215 | Windows Kernel | Kernel object freed, handle table still references (in wild) |
| 157 | CVE-2025-24983 | Windows Win32k | Win32k object freed, subsystem still holds pointer |
| 158 | CVE-2025-32709 | Windows WinSock | Socket ancillary data freed, still accessible |
| 159 | CVE-2025-55678 | Windows DirectX | Graphics kernel object freed, still referenced |
| 160 | CVE-2024-36971 | Linux Kernel | `__dst_negative_advice()`: dst entry freed, route cache holds stale ref |
| 161 | CVE-2023-32233 | Linux nf_tables | Anonymous set freed, still referenced from rule |
| 162 | CVE-2022-32250 | Linux nf_tables | Expression freed, still in active set element |
| 163 | CVE-2023-6932 | Linux IPv4 IGMP | IGMP timer fires on freed multicast group |
| 164 | CVE-2023-20938 | Android Binder | Binder transaction buffer freed, refcount desynced |
| 165 | CVE-2023-21255 | Android Binder | Incomplete fix for CVE-2023-20938; refcount still desyncs |

### Pattern 5: State Machine / Mode Flag Desync

An `is_authenticated`, `is_encrypted`, `state`, or refcount variable doesn't match the actual object/system state. This includes: protocol state machines where transitions are violated, authentication bypass via state confusion, capability/permission flags that don't reflect reality, and refcount leaks.

| # | CVE | Software | What desynced |
|---|-----|----------|---------------|
| 166 | CVE-2023-44487 | HTTP/2 (all impls) | Rapid Reset: stream state machine abused; reset before server processes → 201M req/s DDoS |
| 167 | CVE-2023-48795 | SSH (Terrapin) | Sequence number manipulation: encrypted channel state desynced from actual bytes |
| 168 | CVE-2024-6387 | OpenSSH | Signal handler races with main loop state; `is_in_malloc` flag desynced → root RCE |
| 169 | CVE-2016-5195 | Linux (Dirty COW) | COW state flag race: page marked COW but write proceeds |
| 170 | CVE-2019-17023 | Firefox TLS | Invalid state transition after HelloRetryRequest |
| 171 | CVE-2025-61730 | Go TLS 1.3 | Encryption level boundary timing: state says "encrypted" before key is active |
| 172 | CVE-2023-46805 | Ivanti VPN | Authentication state bypass: `is_authenticated` flag not checked on certain paths |
| 173 | CVE-2024-27198 | TeamCity | Auth state bypass via .jsp path manipulation |
| 174 | CVE-2024-4358 | Telerik Report | Auth state bypass: session created without authentication |
| 175 | CVE-2024-0204 | GoAnywhere MFT | Auth bypass: admin account creation without auth state check |
| 176 | CVE-2022-0185 | Linux Kernel | `CAP_SYS_ADMIN` bypass: capability flag checked in wrong namespace context |
| 177 | CVE-2022-0492 | Linux cgroups | Missing `CAP_SYS_ADMIN` check: capability flag not verified |
| 178 | CVE-2023-0386 | Linux OverlayFS | Capability bits preserved on nosuid mount: permission flags desynced from mount flags |
| 179 | CVE-2023-45866 | Bluetooth | Pairing state machine bypass: device accepted without proper pairing on Android/Linux/macOS |
| 180 | CVE-2024-21306 | Bluetooth HID | Pairing state skipped: HID device connects without user confirmation |
| 181 | CVE-2023-24023 | Bluetooth (BLUFFS) | Session key state: downgraded to brute-forceable key without state flag update |
| 182 | CVE-2022-28356 | Linux net/llc | Refcount leak: `llc_ui_bind` increments but error path doesn't decrement |
| 183 | CVE-2023-4133 | Linux cxgb4 | Refcount UAF in `ch_flower_stats_cb()` |
| 184 | CVE-2023-3567 | Linux vcs | Race condition: `vcs_read` accesses freed tty because state flag not updated atomically |
| 185 | CVE-2023-6546 | Linux GSM | Race: GSM multiplexing state transitions not atomic → privilege escalation |
| 186 | CVE-2023-52638 | Linux CAN j1939 | Deadlock: lock state inconsistent between `j1939_socks_lock` readers/writers |
| 187 | CVE-2023-36424 | Windows CLFS | `eExtendState` field manipulated: log file corruption state desynced |
| 188 | CVE-2023-28252 | Windows CLFS | Same `eExtendState` manipulation, different exploit path |
| 189 | CVE-2024-21302 | Windows Secure Kernel | File version state bypassed: rollback protection flag desynced |
| 190 | CVE-2024-30088 | Windows Kernel | TOCTOU: security state checked, then changed before use |
| 191 | CVE-2023-5981 | GnuTLS | RSA timing: internal state leaks via response time (state ≠ constant-time) |
| 192 | CVE-2024-0553 | GnuTLS | RSA-PSK: incomplete fix; timing still leaks state |
| 193 | CVE-2024-0567 | GnuTLS | Certificate chain validation: `is_valid` flag set incorrectly |
| 194 | CVE-2023-0767 | NSS | PKCS#12: arbitrary write because import state machine doesn't validate bounds |
| 195 | CVE-2024-0901 | wolfSSL | TLS 1.3: client state machine allows read in wrong state → SEGV |
| 196 | CVE-2024-1597 | PostgreSQL JDBC | `PreferQueryMode` state allows SQL injection when mode flag desynced |
| 197 | CVE-2019-5736 | runc | Container state: `/proc/self/exe` overwritten because exec state not locked |
| 198 | CVE-2025-31133 | runc | `maskedPaths` symbolic link: filesystem state doesn't match expected mask state |
| 199 | CVE-2025-52565 | runc | `/dev/pts` mount validation bypass: mount state flag desynced |
| 200 | CVE-2024-10220 | Kubernetes | `gitRepo` volume: command execution because content-state assumed safe |

## Distribution Summary

| Pattern | Count | % of Total |
|---------|-------|-----------|
| **Buffer + separate length** | 45 | 22.5% |
| **External type discriminant** | 42 | 21.0% |
| **Return value / errno encodes other's state** | 38 | 19.0% |
| **Parent/child lifetime dependency** | 40 | 20.0% |
| **State machine / mode flag desync** | 35 | 17.5% |

## Key Observations

**1. This is arguably the #1 vulnerability class in systems software.** Every single pattern above is fundamentally the same bug: Variable A's semantic meaning depends on Variable B, and they went out of sync. C and C++ have no language-level mechanism to enforce these cross-variable invariants.

**2. The JIT compiler pattern (42 type confusion CVEs) is the purest form.** In V8/SpiderMonkey/JSC, the JIT compiler maintains a *separate* type assumption (the "map" or "shape" or "structure") about an object. When speculation is wrong, the external type tag says "int array" but the object is actually "object array" — classic external discriminant desync.

**3. Heartbleed is the canonical buffer+length case.** The `payload_length` field in the heartbeat message is an *external* statement about how much data follows. The server trusted it without comparing to the *actual* record length — two variables describing the same buffer, desynced.

**4. The EVP_VerifyFinal cluster (CVE-2008-5077 + 8 downstream CVEs) shows cascading impact.** One API where the return value encodes the validity of the signature object, and *nine separate projects* got it wrong. The return value is external state about the verification result, and the three-valued return (-1, 0, 1) was misread as boolean.

**5. The lifetime/UAF pattern (40 CVEs) is exactly what DTT's `VarToResource` already models** — just not generalized enough. Every browser UAF is "pointer A depends on object B being alive; B was freed." DTT already tracks `pointer → resource → {freed: true}`. The gap is tracking non-heap dependencies: iterators depending on containers, callbacks depending on contexts, child objects depending on parent arenas.

## Implications for DTT

These 200 CVEs suggest three high-value extensions:

**1. Ghost Span / Buffer+length binding (covers Pattern 1: 45 CVEs)**

When DTT sees `malloc(n)` and a separate `size_t len = n`, bind `len` as `buf._size`. When `buf = realloc(buf, m)`, check that `len` is updated. This alone would catch Heartbleed-class bugs.

The concept: `std::span` is a language-level admission that buffer+length is a single semantic unit that C got wrong by splitting into two variables. A "ghost span" would be DTT internally recognizing that a separate length variable is an alias for a buffer's `_size` synthetic field.

**2. Return-value-describes-argument binding (covers Pattern 3: 38 CVEs)**

When `read(fd, buf, n)` returns `ret`, bind `ret` as the actual `buf._size` and `ret == -1` as `fd.@error`. This catches the entire unchecked-return-value class.

**3. Lifetime association generalization (covers Pattern 4: 40 CVEs)**

Generalize `VarToResource` so that when `it = v.begin()`, DTT creates an association `it → v.buffer_generation`. When `v.push_back()` bumps the generation, `it` becomes invalid. This is the iterator invalidation detector.

## Ghost Span: Deep Dive

### Every modern language figured this out

`std::span`, `std::string_view`, Rust slices (`&[T]`), Go slices (`[]T`), Python's `memoryview`, C#'s `Span<T>`, Swift's `UnsafeBufferPointer` — every language eventually realizes that `(pointer, length)` is an **atomic semantic unit**. They all independently arrived at the same abstraction: a fat pointer that bundles the buffer address with its bounds.

C got this wrong in 1972. Fifty years and billions of dollars in damage later, every successor language treats it as a single entity. The 45 buffer+length CVEs in Pattern 1 are the direct cost of that original sin.

A ghost span would enforce that invariant *retroactively* on existing C/C++ code that splits pointer and length into separate variables — which is essentially all of it.

### How it works

Today DTT tracks synthetic fields attached to objects:
```
buf._size = n        // synthetic field attached to buf
buf._nonnull = 1     // synthetic field attached to buf
```

The gap is when the size lives in a *separate variable*:
```c
char* buf = malloc(n);
size_t len = n;          // This IS buf._size, but DTT doesn't know that
```

A ghost span creates a **binding**:
```
len  ←→  buf._size       // bound: len is an alias for buf._size
```

After this binding, DTT's existing machinery kicks in automatically:
- `buf = realloc(buf, m)` → DTT knows `buf._size` changed → checks if `len` was updated
- `memcpy(buf, src, len)` → DTT resolves `len` to `buf._size` → bounds check works
- `len = 0; buf[0] = x;` → DTT sees `buf._size == 0` → flags OOB access

**No new analysis engine needed.** Just a binding layer that says "this variable is really a ghost field of that variable."

### Three sources of bindings

**Source 1: Allocation patterns (automatic)**
```c
char* buf = malloc(n);     // DTT infers: buf._size = n
size_t len = n;            // DTT infers: len ≡ buf._size (same RHS)
```
When two variables are assigned from the same expression in proximity to an allocation, DTT creates the binding.

**Source 2: Function signatures (API model / annotation)**
```c
// DTT knows: read() → ret encodes buf's actual populated size
ssize_t ret = read(fd, buf, count);
// Ghost binding: ret ←→ buf._size (on success)
```
This extends the existing API modeling system. The annotation vocabulary just needs richer cross-variable relationship descriptions.

**Source 3: Struct field patterns (heuristic)**
```c
struct buffer {
    char* data;
    size_t len;     // DTT infers: len ≡ data._size (naming + type + adjacency)
};
```
Adjacent fields where one is a pointer and the other is an integer with a size-like name (`len`, `size`, `count`, `capacity`, `length`, `n`, `num`, `nb`) create the binding.

### What ghost spans catch directly

From the 200 CVEs in this report, ghost spans would have caught at minimum:

| CVE | Bug | Ghost span binding that catches it |
|-----|-----|------------------------------------|
| CVE-2014-0160 (Heartbleed) | `payload_length` trusted over `record.length` | `payload_length ←→ payload._size` |
| CVE-2023-38545 (curl SOCKS5) | hostname stored in wrong-sized buffer | `hostnameLen ←→ hostname._size` |
| CVE-2021-3156 (sudo) | escaped chars not counted in buffer size | `size ←→ user_args._size` |
| CVE-2020-0796 (SMBv3) | header says 0x1100, alloc gets 0x63 | `header.length ←→ buf._size` |
| CVE-2024-38063 (Windows IPv6) | 0 wraps to 0xFFFF | `pkt_len ←→ buf._size` |
| CVE-2020-16898 (Bad Neighbor) | offset advances wrong amount | `offset ←→ remaining._size` |
| CVE-2021-39685 (Linux USB) | `wLength` unbounded | `wLength ←→ buf._size` |
| CVE-2016-3955 (Linux USB/IP) | `urb->actual_length` from network | `actual_length ←→ transfer_buffer._size` |
| CVE-2021-3712 (OpenSSL ASN1) | length field ≠ NUL position | `ASN1_STRING.length ←→ data._size` |
| CVE-2024-12084 (Rsync) | `s2length` not validated | `s2length ←→ checksum_buf._size` |

These alone are CVSS 9.0+ bugs responsible for billions in damage.

## The Unifying Abstraction

All five patterns share the same structure:

> **Variable A's semantic meaning depends on Variable B's value, and they can go out of sync.**

Currently DTT has two tracking models:
- **TypedEnv**: `object.property → value` (state is attached to the object)
- **VarToResource**: `pointer → resource_id → state` (state is shared via indirection)

The second model is already the right shape for external state. The question is generalizing it into a **State Association** mechanism where any variable can be bound to shared state that other variables also reference — and when any participant changes, all dependents are checked.

```
                    ┌─────────────────────┐
Variable A ──────►  │   Association(id)    │
Variable B ──────►  │   { prop1: val1,     │
                    │     prop2: val2 }    │
                    └─────────────────────┘
```

This is conceptually identical to how `VarToResource` already works for pointers and heap objects — just generalized beyond the heap.
