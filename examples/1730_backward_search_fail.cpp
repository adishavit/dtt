// Test: Backward search should suggest operations that establish failed preconditions
// DTT should emit errors AND additional notes showing what operations could fix each
// violation, dynamically discovered from the annotation registry (not a hardcoded table).
//
// For each error, DTT should search MethodAnnotations/FreeFunctionAnnotations
// for postconditions that establish the required property, and boolean guards
// (ensures_if_true/false) that would prove it.
//
// Expected notes (backward search results):
//   "could establish _size > 0: push_back(), emplace_back(), resize(n) with n > 0"
//   "could guard: if (!v.empty()) { ... } [proves _size > 0]"
//   "could establish _has_value: emplace(...), operator=(val)"
//   "could guard: if (opt.has_value()) { ... }"

#include <vector>
#include <optional>

// Variant 1: empty vector access — backward search should find push_back, resize, etc.
void variant1_empty_vector() {
    std::vector<int> v;
    int x = v.front();  // ERROR: _size > 0 not satisfied  dtt-error {{_size > 0}}
    // backward search should suggest: push_back, emplace_back, resize(n>0), ...
    // backward search should suggest guard: if (!v.empty()) { ... }
}

// Variant 2: optional without value — backward search should find emplace, operator=
void variant2_empty_optional() {
    std::optional<int> opt;
    int x = opt.value();  // ERROR: _has_value == 1 not satisfied  dtt-error {{_has_value == 1}}
    // backward search should suggest: emplace(...), operator=(val)
    // backward search should suggest guard: if (opt.has_value()) { ... }
}

// Variant 3: null pointer — backward search should suggest guard
void variant3_null_pointer() {
    int* p = nullptr;
    int x = *p;  // ERROR: _nonnull not satisfied  dtt-error {{potential null pointer dereference}}
    // backward search should suggest guard: if (p) { ... }
}

// Variant 4: vector cleared then accessed — backward search suggests re-fill
void variant4_cleared_vector() {
    std::vector<int> v(5);
    v.clear();
    int x = v.front();  // ERROR: _size > 0 not satisfied after clear  dtt-error {{_size > 0}}
    // backward search should suggest: push_back, resize, etc. to re-establish _size > 0
}
