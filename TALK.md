# Impossible Types, Supernatural Guarantees

## Bringing Dependent Types to C++

---

## Act 1: The Haunting (5 min)

### Open with a Ghost Story

> "In 2014, a ghost escaped from OpenSSL. It had been hiding in plain sight for two years. When it finally revealed itself, it had read the private keys of 17% of the internet's secure servers. We called it Heartbleed."

**Show the actual bug (simplified):**

```cpp
// The ghost
memcpy(response, payload, payload_length);  // payload_length came from attacker
```

### The Punchline

> "The fix was one line. The damage was $500 million. And your compiler said nothing."

### Transition

> "Tonight I'm going to show you how to make your compiler see ghosts."

---

## Act 2: The Séance (3 min)

### What Are Dependent Types?

> "In languages like Idris and Agda, you can write `Vector 5 Int` - a vector that is *typed* as having exactly 5 elements. Try to access element 6? Not a runtime error. Not even a compile error. It's a *type* error. The program is ill-formed."

> "C++ people hear this and say: 'That's nice. We have `std::array<int, 5>`.' But that's not the same thing..."

**Show the difference:**

```cpp
std::array<int, 5> arr;        // Size is a compile-time constant
std::vector<int> vec;          // Size is a runtime value
```

> "Here's the thing: `std::array` cheats. The `5` is a template parameter - it's baked into the type at compile time. That's easy."

> "But `std::vector`? Its size is a runtime value. The compiler can't know it."

**The `argc` example:**

```cpp
int main(int argc, char* argv[]) {
    std::vector<int> v;
    v.resize(argc);    // argc is the ultimate runtime value
    v[0] = 42;         // Safe? Only if argc > 0.
}
```

> "`argc` comes from the command line. The compiler has *no idea* what it will be. Could be 0, could be 100. Traditional dependent types can't help you here - they need static values."

> "That's the 'impossible' part of 'impossible types'."

### The Hook

> "What if your compiler could track `argc` symbolically?"

> "It can't prove `argc > 0` - that's unknowable. But it *can* learn from your guards:"

```cpp
if (argc > 0) {
    v[0] = 42;  // ✓ Now DTT knows argc > 0 here
}
```

> "Your runtime check becomes a compile-time proof."

---

## Act 3: The Summoning (10 min)

### Live Demo - Build Tension

```cpp
#include <vector>

int main() {
    std::vector<int> v;
    v.push_back(42);
    return v.front();  // Is this safe?
}
```

Compile it. **It passes.**

Now:

```cpp
#include <vector>

int main() {
    std::vector<int> v;
    return v.front();  // What about now?
}
```

**Compile. Watch it fail.**

```
error: DTT precondition not satisfied: requires '_size > 0' but '_size = 0'
```

> "The compiler just told you that you're accessing an empty vector. At compile time. With zero runtime cost."

**Pause for effect:**

> "And yes - that's `std::vector`. Not some special wrapper. Your actual, normal, everyday `std::vector`."

### Reveal the Magic

DTT auto-annotates the standard library. Under the hood, it's as if you wrote:

```cpp
[[clang::annotate("dtt:requires=_size > 0")]]
reference front();

[[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
void push_back(const T& value);
```

> "DTT ships with annotations for `vector`, `map`, `optional`, `string`, `unique_ptr`, and more. You don't change your code. You just compile with the plugin, and your compiler starts seeing ghosts."

### What's Auto-Annotated (Optional Slide)

| Type | Tracked Property |
|------|------------------|
| `vector`, `deque`, `list`, `string` | `_size` (bounds, empty) |
| `map`, `set`, `unordered_*` | `_size` (empty checks) |
| `optional`, `any`, `variant` | `_has_value` |
| `unique_ptr`, `shared_ptr` | `_nonnull` |
| `array`, `span`, `string_view` | bounds via `_size` |
| `stack`, `queue`, `priority_queue` | `_size` (top/front checks) |

> "50+ methods across 20+ types. All automatic. All zero runtime cost."

---

## Act 4: The Exorcism (8 min)

### Demo 1: The Tracking

```cpp
std::vector<int> v;          // _size = 0
v.push_back(1);              // _size = 1
v.push_back(2);              // _size = 2
v.pop_back();                // _size = 1
v[0] = 10;                   // ✓ 0 < 1
v[1] = 20;                   // ✗ ERROR: 1 < 1 is false
```

### Demo 2: The Guard (The Mind-Blow)

```cpp
void safe_access(std::vector<int>& v, size_t i) {
    if (i < v.size()) {
        v[i] = 42;  // ✓ DTT learns from the if-condition!
    }
}
```

> "DTT doesn't just track values. It *learns from your code*. That `if` statement isn't just a runtime check anymore - it's a *proof*."

### Demo 3: Symbolic Reasoning

```cpp
void fill(std::vector<int>& v, size_t n) {
    v.resize(n);           // _size = n (symbolic!)
    for (size_t i = 0; i < n; ++i) {
        v[i] = i;          // ✓ i < n, and n == _size
    }
}
```

> "We don't know what `n` is. The compiler doesn't know. But Z3 knows that `i < n` when `_size == n`. That's supernatural."

---

## Act 5: The Bestiary (5 min)

### Beyond Bounds Checking

> "So far I've shown you bounds checks. But DTT isn't a bounds checker. It's a *property tracker*. Any integer or boolean property you can express, DTT can track."

> "Let me show you five more ghosts DTT can see."

### Example 1: Use-After-Move

> "Show of hands: who's ever used a vector after moving from it?"

```cpp
std::vector<int> v1;
v1.resize(5);              // v1._size = 5

std::vector<int> v2;
v2 = std::move(v1);        // v2._size = 5, v1 = UNKNOWN

v2[4];                     // ✓ OK: v2 has size 5
v1.front();                // ✗ ERROR: v1._size is Unknown
```

> "After a move, the source is marked Unknown. DTT won't let you touch it. Use-after-move, caught at compile time."

### Example 2: std::optional

> "Modern C++ pain: calling `.value()` without checking."

```cpp
std::optional<int> opt;
opt.value();               // ✗ ERROR: _has_value is 0

opt = 42;                  // _has_value = 1
opt.value();               // ✓ OK

if (opt.has_value()) {
    opt.value();           // ✓ OK: guard proves _has_value
}
```

> "DTT tracks `_has_value` through assignments, `reset()`, `emplace()`. It learns from `has_value()` guards. No more 'bad optional access' at 3am."

### Example 3: Use-After-Free

> "The CVE factory. Memory freed, pointer still around."

```cpp
char* buf = (char*)malloc(100);
strcpy(buf, "Hello");
free(buf);

buf[0] = 'X';              // ✗ ERROR: use-after-free of 'buf'
strcpy(buf, "World");      // ✗ ERROR: use-after-free of 'buf'
strlen(buf);               // ✗ ERROR: use-after-free of 'buf'
```

> "DTT tracks pointer lifetime. When you call `free(p)`, that pointer is marked dead. Any use — dereference, array access, even passing to `strcpy` — is caught. No AddressSanitizer needed. Compile-time."

### Example 4: State Machines

> "Ever called `send()` on a socket that wasn't connected?"

```cpp
Connection conn;           // _state = 0 (closed)
conn.send("hello");        // ✗ ERROR: _state is 0, requires 2
conn.connect();            // _state = 1 (open)
conn.authenticate();       // _state = 2 (authenticated)
conn.send("hello");        // ✓ OK: _state is 2
```

> "The protocol is encoded in the types. You literally cannot send on an unauthenticated connection. The compiler won't let you."

### Example 5: SQL Injection (Bobby Tables)

> "Little Bobby Tables, we call him."

```cpp
UserInput input("Robert'); DROP TABLE Students;--");

input.as_sql_param();      // ✗ ERROR: _sql_escaped is 0

input.escape_sql();        // _sql_escaped = 1
input.as_sql_param();      // ✓ OK: properly escaped
```

> "The constraint is simple: you can't use user input in SQL until it's escaped. DTT enforces it. Bobby Tables is foiled at compile time."

### Example 6: Ghost State (Unforgeable Crypto)

> "Here's my favorite trick. The `@` prefix."

```cpp
struct SecureData {
    // No @encrypted field exists in the struct!

    [[clang::annotate("dtt:ensures=@encrypted == 1")]]
    void encrypt();

    [[clang::annotate("dtt:requires=@encrypted == 1")]]
    void send();
};

SecureData secret;
secret.send();             // ✗ ERROR: @encrypted is 0
secret.encrypt();
secret.send();             // ✓ OK
```

> "The `@` prefix means *ghost state* - it exists only in the compiler's tracking. There's no `secret.@encrypted` field you can write to. It's unforgeable. You can't cheat your way past it."

### The Pattern

> "Same mechanism. Different properties."

| Ghost | What DTT Tracks |
|-------|-----------------|
| Use-after-move | Source becomes `Unknown` |
| Empty optional | `_has_value` |
| Protocol state | `_state` (0 → 1 → 2) |
| SQL injection | `_sql_escaped` |
| Crypto | `@encrypted` (unforgeable) |

> "If you can write a contract, DTT can enforce it."

---

## Act 6: The Catch (3 min)

### Honesty Round

> "DTT isn't perfect. The STL is auto-annotated, but your own classes need manual annotations. Range-for loops don't establish proofs yet. And sometimes the Z3 solver throws up its hands."

> "But here's the thing: it never lies. If it says there's a bug, there's a bug. If it can't prove safety, it warns you. It's conservative, not optimistic."

### The Philosophy

> "DTT sits in a sweet spot: stronger than sanitizers (no runtime cost), more practical than full verification (no PhD required), and it works on *your existing C++ code*."

---

## Act 7: The Call (2 min)

### The Memorable Ending

> "Heartbleed was a one-line bug that hid for two years. The compiler saw it compile. The tests saw it run. The code reviewers saw it merge. Nobody saw the ghost."

> "With dependent types, that ghost would have been visible from the start. Not as a warning. Not as a runtime crash. As a *type error*. Impossible to ignore. Impossible to ship."

> "C++ has been haunted by undefined behavior for 40 years. Maybe it's time we learned to see the ghosts."

**End with:** GitHub link, QR code, "questions?"

---

## Speaker Notes

### Memorable Moments to Nail

1. **The Heartbleed open** - Real stakes, real money, real damage
2. **The first failed compile** - The "aha" moment when the audience sees it work
3. **The std::vector reveal** - "That's your actual std::vector" - no wrapper types needed
4. **The guard demo** - "It learns from your code" is the mind-blow
5. **The Z3 reveal** - "We have an SMT solver in our compiler" sounds insane (because it is)
6. **Use-after-move** - "Show of hands" gets participation, then the catch
7. **Bobby Tables** - Everyone laughs, then realizes it's serious
8. **Ghost state (`@`)** - "Unforgeable" is magic word for security folks

### One-Liners to Sprinkle In

- "The best runtime check is the one that never runs."
- "std::vector knows its size. Your compiler just refuses to remember."
- "In C++, we don't have dependent types. We have *independent* types - they do their own thing."
- "Z3 is an SMT solver. It's like having a math PhD on retainer, but faster and less argumentative."
- "Sanitizers find bugs when they happen. DTT finds bugs when they're written."
- "No special types. No wrapper classes. Just your code, plus a plugin, equals compile-time safety."
- "Your runtime check isn't just a check anymore - it's a proof."
- "Bobby Tables is foiled at compile time."

### Potential Demo Failures - Have Backups

- Screenshots of successful error catches
- The Z3 solving in action
- A real xfail test becoming a fail test

### Slide Design Tips

- Dark theme with green "ghost" accents (supernatural theme)
- Minimal text - voice carries the story, slides show code
- Error messages in red - make them dramatic
- Before/after splits - show the unsafe code, then the caught error

---

## Timing Summary

| Act | Topic | Duration |
|-----|-------|----------|
| 1 | The Haunting (Heartbleed) | 5 min |
| 2 | The Séance (What are dependent types?) | 3 min |
| 3 | The Summoning (First demo) | 10 min |
| 4 | The Exorcism (Advanced demos) | 8 min |
| 5 | The Bestiary (What else it catches) | 5 min |
| 6 | The Catch (Limitations) | 3 min |
| 7 | The Call (Closing) | 2 min |
| | **Total** | **~36 min** |

Leaves ~10 min for Q&A in a 45-min slot, or breathing room in a 40-min slot.

---

## Demo Scripts

### Core Demos (Act 3-4)

```bash
# Empty vector access - FAILS
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/02_basic_fail.cpp

# Guard learning - PASSES
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/03_runtime_guards_pass.cpp
```

### Act 5 Demos (The Bestiary)

```bash
# Use-after-move
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/70_move_invalidate_pass.cpp  # PASS
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/71_move_invalidate_fail.cpp  # FAIL

# std::optional
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/804_std_optional_pass.cpp   # PASS

# Use-after-free (C library functions)
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/714_uaf_clib_args_fail.cpp  # FAIL

# State machine
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/960_state_machine_pass.cpp  # PASS
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/961_state_machine_fail.cpp  # FAIL

# SQL injection
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/972_input_validation_pass.cpp  # PASS
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/973_input_validation_fail.cpp  # FAIL

# Ghost state (@encrypted)
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/493_ghost_state_pass.cpp   # PASS
clang++-18 -fplugin=./build/libDTTPlugin.so -std=c++20 -fsyntax-only examples/494_ghost_state_fail.cpp   # FAIL
```

---

## Backup Slides

### If Demo Fails

Have pre-recorded GIF or screenshot of each demo working.

### If Time Runs Short

Cut Act 5 (Bestiary) - mention it briefly, point to docs.

### If Time Runs Long

Shorten Act 4 to just the Guard demo - it's the strongest.

---

## Q&A Prep

### Expected Questions

**Q: How does this compare to C++26 contracts?**
> Contracts are runtime checks. DTT is compile-time proofs. They're complementary - contracts catch what DTT can't prove, DTT eliminates checks contracts would need.

**Q: What's the compile-time overhead?**
> Depends on code complexity. Simple code: negligible. Heavy Z3 usage: noticeable but manageable. We're optimizing.

**Q: Do I need special wrapper types?**
> No! That's the whole point. DTT auto-annotates `std::vector`, `std::string`, `std::map`, `std::optional`, `std::unique_ptr`, and dozens more. You write normal C++. The plugin does the rest.

**Q: What about templates?**
> Works with templates. DTT tracks template parameters and can reason about them symbolically.

**Q: Why not just use Rust?**
> (Laugh) Rust solves ownership. DTT solves value properties. Different problems. Also: 50 years of C++ code isn't rewriting itself.

**Q: Is this production-ready?**
> Experimental but functional. We use it. You can try it. Contributions welcome.
