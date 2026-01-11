// WHAT IS TESTED:
// Type range bounds - FALSE POSITIVE cases where DTT warns but code is safe.
// DTT should infer that type range guarantees valid index.
//
// Expected behavior: PASS (type range guarantees valid index)
// Current behavior: DTT recognizes type range (false positive - doesn't use type range)
//
// NOTE: This is an OK because DTT currently produces errors on safe code.
// When the gap is fixed, this test should PASS (no errors).
//
// ============================================================================
// THE GAP (FALSE POSITIVES)
// ============================================================================
// unsigned char  -> 0-255   -> [256] array access is always safe
// unsigned short -> 0-65535 -> [65536] array access is always safe
//
// DTT doesn't infer: "idx is unsigned char" => "idx < 256" always true
// ============================================================================

#include <cstdint>

// -----------------------------------------------------------------------------
// VARIANT 1: Function parameter loses type constraint
// -----------------------------------------------------------------------------
int variant1_param(unsigned char idx) {
    static int data[256];
    return data[idx];  // OK: should pass, param type guarantees 0-255
}

// -----------------------------------------------------------------------------
// VARIANT 2: Cast expression loses type info
// -----------------------------------------------------------------------------
enum class Color : uint8_t { Red, Green, Blue };

void variant2_cast_expr() {
    const char* names[256];
    Color c = Color::Red;

    names[static_cast<uint8_t>(c)] = "red";  // OK: cast result is uint8_t
}

// -----------------------------------------------------------------------------
// VARIANT 3: External input with type constraint
// -----------------------------------------------------------------------------
unsigned char get_byte();  // Returns 0-255 by type

void variant3_external() {
    int table[256];
    unsigned char idx = get_byte();

    table[idx] = 42;  // OK: get_byte() returns uchar, must be 0-255
}

int main() {
    (void)variant1_param(10);
    variant2_cast_expr();
    variant3_external();
    return 0;
}
