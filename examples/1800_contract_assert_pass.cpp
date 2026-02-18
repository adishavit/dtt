// Contract assertion proof hints — dtt_contract_assert(expr) injects expr as
// a proof fact, allowing DTT to prove subsequent operations safe even when
// normal tracking is insufficient.
//
// This is the Clang-18 equivalent of C++26 contract_assert (P2900).
// DTT translates the expression AST (Expr*) into proof facts via the
// same guard infrastructure used for if-condition narrowing.

#include <vector>
#include <optional>

// Contract assertion: DTT analyzes the argument expression and injects
// the extracted constraint as a proof fact. No-op at runtime.
static inline void dtt_contract_assert(bool) {}

// Variant 1: Assert non-empty, then access front
void test_vector_nonempty() {
    std::vector<int> v;                   // _size = 0
    dtt_contract_assert(v.size() > 0);    // Proof: _size > 0
    v.front();                            // Safe: proof overrides _size=0
}

// Variant 2: Assert pointer non-null, then dereference
void test_pointer_nonnull() {
    int* p = nullptr;                     // _nonnull = 0
    dtt_contract_assert(p != nullptr);    // Proof: _nonnull != 0
    *p = 42;                              // Safe: proof overrides _nonnull=0
}

// Variant 3: Assert optional has value, then access
void test_optional_has_value() {
    std::optional<int> o;                 // _has_value = 0
    dtt_contract_assert(o.has_value());   // Proof: _has_value != 0
    o.value();                            // Safe: proof overrides _has_value=0
}

// Variant 4: Assert with !empty() (negation of bool method)
void test_not_empty() {
    std::vector<int> v;                   // _size = 0
    dtt_contract_assert(!v.empty());      // Proof: _size > 0 (negated empty)
    v.front();                            // Safe
}

// Variant 5: Assert size >= 2
void test_size_ge() {
    std::vector<int> v;                   // _size = 0
    dtt_contract_assert(v.size() >= 2);   // Proof: _size >= 2
    v[1];                                 // Safe: _size >= 2 means index 1 valid
}

int main() { return 0; }
