# DTT Stdlib Annotation Gaps

Gaps identified during TDD test creation for `feature/stdlib-annotations-v2` branch.

## TDD Test Results Summary (Updated)

After implementing synthetic annotations in `registerSyntheticAnnotations()`:

**Xfail tests now PASSING (DTT catches issues):**
| Test | Status | What DTT Does |
|------|--------|---------------|
| 815_std_array_xfail | ✓ PASS | DTT catches array bounds |
| 817_std_span_xfail | ✓ PASS | DTT catches span bounds |
| 801_std_vector_xfail | ✓ PASS | DTT catches vector front/back/operator[] |
| 803_std_string_xfail | ✓ PASS | DTT catches string front/back/operator[] |
| 827_std_stack_queue_xfail | ✓ PASS | DTT catches stack/queue top/front/back |
| 829_std_string_view_xfail | ✓ PASS | DTT catches string_view bounds |
| 831_std_variant_xfail | ✓ PASS | DTT catches variant valueless |
| 833_std_any_xfail | ✓ PASS | DTT catches any has_value |
| 835_std_weak_ptr_xfail | ✓ PASS | DTT catches weak_ptr expired |
| 839_malloc_xfail | ✓ PASS | DTT catches malloc/free issues |
| 849_heartbleed_xfail | ✓ PASS | DTT catches null pointer issues |
| 850_buffer_overread_xfail | ✓ PASS | DTT catches null pointer issues |
| 851_integer_overflow_size_xfail | ✓ PASS | DTT catches null pointer issues |

**Xfail tests still FAILING (gaps remain):**
| Test | Status | Gap Description |
|------|--------|-----------------|
| 805_std_optional_xfail | ✗ FAIL | Need has_value postcondition tracking |
| 807_std_unique_ptr_xfail | ✗ FAIL | Need nonnull postcondition tracking |
| 809_std_shared_ptr_xfail | ✗ FAIL | Need nonnull postcondition tracking |
| 811_memcpy_xfail | ✗ FAIL | Need buffer bounds tracking |
| 813_strcpy_xfail | ✗ FAIL | Need string bounds tracking |
| 823_std_set_map_xfail | ✗ FAIL | Iterator tracking gap |
| 825_std_unordered_xfail | ✗ FAIL | Iterator tracking gap |
| 837_std_fstream_xfail | ✗ FAIL | File stream state tracking |
| 841_fopen_xfail | ✗ FAIL | C file IO state tracking |
| 843_scanf_xfail | ✗ FAIL | scanf return value tracking |
| 845_mutex_xfail | ✗ FAIL | Mutex state tracking |
| 847_future_xfail | ✗ FAIL | Future state tracking |

**Working:** std::array, std::span, std::vector, std::string, std::string_view, std::deque, std::list, std::stack, std::queue, std::variant, std::any, std::weak_ptr (basic preconditions)

**Gaps:** Smart pointer postconditions, iterator tracking, file IO, synchronization primitives

## Gap Categories

### 1. Ternary Conditional Bounds Propagation

**Pattern:**
```cpp
return index < arr.size() ? arr[index] : -1;  // DTT doesn't track bounds
```

**Current Behavior:** DTT doesn't propagate bounds constraints through ternary conditional expressions.

**Expected Behavior:** In `cond ? expr1 : expr2`, if `cond` establishes bounds (e.g., `index < size()`), that constraint should apply to `expr1`.

**Workaround:** Use if-else instead of ternary:
```cpp
if (index < arr.size()) { return arr[index]; }
return -1;
```

---

### 2. Smart Pointer `.get()` Tracking

**Pattern:**
```cpp
auto ptr = std::make_unique<int>(42);
int* raw = ptr.get();
*raw = 100;  // DTT error: raw nullability unknown
```

**Current Behavior:** Raw pointer from `.get()` doesn't inherit the smart pointer's nullability state.

**Expected Behavior:** If `ptr` is known non-null (e.g., from `make_unique`), then `ptr.get()` should return a non-null pointer.

**Workaround:** Guard the raw pointer:
```cpp
if (raw) { *raw = 100; }
```

---

### 3. Iterator Tracking

**Pattern:**
```cpp
for (auto it = arr.begin(); it != arr.end(); ++it) {
    *it = 42;  // DTT error: iterator nullability unknown
}
```

**Current Behavior:** Iterators are treated as raw pointers with unknown nullability.

**Expected Behavior:** Iterators obtained from `begin()`/`end()` should be valid within their loop bounds.

**Workaround:** Use range-based for:
```cpp
for (auto& elem : arr) { elem = 42; }
```

---

### 4. std::span Size Tracking (ALL dynamic spans)

**Pattern:**
```cpp
int arr[100]{};
std::span<int> s(arr);  // Dynamic extent
if (index < s.size()) {
    s[index] = 42;  // DTT error: _size = ?
}
```

**Current Behavior:** DTT shows `_size = ?` for ALL dynamic extent spans, even from local arrays.

**Expected Behavior:**
- Spans from local arrays should have known size
- `if (index < s.size())` should establish bounds constraint

**Workaround:**
- Use static extent spans: `std::span<int, 10> s(arr);`
- Use range-based for loops (no operator[] needed)

**Scope:** This is a MAJOR gap affecting:
- Span from local arrays
- Span from std::vector
- Span from std::array (dynamic extent)
- Span function parameters
- All guarded index access on dynamic spans

---

### 5. find() != npos Implies Valid Index

**Pattern:**
```cpp
std::string_view sv = "hello world";
auto pos = sv.find('w');
if (pos != std::string_view::npos) {
    sv[pos];  // DTT error: pos might be out of bounds
}
```

**Current Behavior:** DTT doesn't understand that `find() != npos` implies the returned position is a valid index.

**Expected Behavior:** When `find()` returns a value != npos, that value must be < size() by definition of find().

**Workaround:** Add explicit size check:
```cpp
if (pos != std::string_view::npos && pos < sv.size()) {
    sv[pos];  // OK
}
```

**Test file:** `829_std_string_view_xfail.cpp` Variant 6.3

---

### 6. Return-Stack-Address Warning with Smart Pointer `.get()`

**Pattern:**
```cpp
int* test_ternary_get() {
    std::unique_ptr<int> ptr = std::make_unique<int>(42);
    return ptr ? ptr.get() : nullptr;  // Warning: returning address of stack memory
}
```

**Note:** This is a compiler warning, not a DTT gap. The smart pointer dies at function end, making the returned pointer dangling.

---

## Implementation Priority

1. **High Priority** - These affect common patterns:
   - Smart pointer `.get()` tracking
   - Iterator tracking
   - Ternary conditional bounds

2. **Medium Priority** - Less common but useful:
   - Dynamic span parameter tracking
   - Span from vector tracking

3. **Low Priority** - Edge cases:
   - Complex expression bounds propagation

---

## Test Files Reference

These gaps are documented in the test files:
- `800_std_vector_pass.cpp` / `801_std_vector_xfail.cpp`
- `802_std_string_pass.cpp` / `803_std_string_xfail.cpp`
- `804_std_optional_pass.cpp` / `805_std_optional_xfail.cpp`
- `806_std_unique_ptr_pass.cpp` / `807_std_unique_ptr_xfail.cpp`
- `808_std_shared_ptr_pass.cpp` / `809_std_shared_ptr_xfail.cpp`
- `810_memcpy_pass.cpp` / `811_memcpy_xfail.cpp`
- `812_strcpy_pass.cpp` / `813_strcpy_xfail.cpp`
- `814_std_array_pass.cpp` / `815_std_array_xfail.cpp`
- `816_std_span_pass.cpp` / `817_std_span_xfail.cpp`
