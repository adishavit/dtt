# DTT Stdlib Annotation Gaps

Documents known gaps in DTT's stdlib tracking. Run `ctest --test-dir build -L xfail` for current status.

**Working:** std::array, std::span, std::vector, std::string, std::string_view, std::deque, std::list, std::stack, std::queue, std::variant, std::any, std::weak_ptr (basic preconditions)

**Known gaps:** Smart pointer `.get()` tracking, iterator tracking, file IO state, synchronization primitives

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
- `810_memcpy_pass.cpp` / `811_memcpy_fail.cpp`
- `812_strcpy_pass.cpp` / `813_strcpy_xfail.cpp`
- `814_std_array_pass.cpp` / `815_std_array_xfail.cpp`
- `816_std_span_pass.cpp` / `817_std_span_xfail.cpp`
