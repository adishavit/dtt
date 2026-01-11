// Example 510: Early return as bounds guard — PASS
// ============================================================================
// Tests that early return statements establish bounds proofs
// ============================================================================
//
// Pattern: if (index < 0 || index > MAX) return default; arr[index];
// After the guard, index is known to be in range [0, MAX]
//
// Expected: All variants should compile WITHOUT DTT errors
// ============================================================================

// -----------------------------------------------------------------------------
// VARIANT 1: OpenSSL ASN1 pattern - if (x < 0 || x > N) return; use(arr[x])
// -----------------------------------------------------------------------------
const char* tag2str[] = {
    "EOC", "BOOLEAN", "INTEGER", "BIT STRING", "OCTET STRING",
    "NULL", "OBJECT", "OBJECT DESCRIPTOR", "EXTERNAL", "REAL",
    "ENUMERATED", "EMBEDDED PDV", "UTF8STRING", "SEQUENCE",
    "SET", "NUMERICSTRING", "PRINTABLESTRING", "T61STRING",
    "VIDEOTEXSTRING", "IA5STRING", "UTCTIME", "GENERALIZEDTIME",
    "GRAPHICSTRING", "ISO64STRING", "GENERALSTRING", "UNIVERSALSTRING",
    "BMPSTRING", "TAG27", "TAG28", "TAG29", "TAG30"
};  // 31 elements (indices 0-30)

const char* variant1_openssl_pattern(int tag) {
    if (tag < 0 || tag > 30)
        return "(unknown)";
    return tag2str[tag];  // tag is now 0..30, array has 31 elements
}

// -----------------------------------------------------------------------------
// VARIANT 2: Reversed condition order
// -----------------------------------------------------------------------------
const char* variant2_reversed_order(int tag) {
    if (tag > 30 || tag < 0)
        return "(unknown)";
    return tag2str[tag];
}

// -----------------------------------------------------------------------------
// VARIANT 3: Using >= instead of >
// -----------------------------------------------------------------------------
const char* names[10] = {"zero", "one", "two", "three", "four",
                         "five", "six", "seven", "eight", "nine"};

const char* variant3_gte_guard(int n) {
    if (n < 0 || n >= 10)
        return "?";
    return names[n];  // n is 0..9
}

// -----------------------------------------------------------------------------
// VARIANT 4: Single upper bound check (unsigned handles lower)
// -----------------------------------------------------------------------------
const char* variant4_unsigned_trick(unsigned idx) {
    if (idx > 30)
        return "(unknown)";
    return tag2str[idx];  // idx is 0..30 (unsigned, so >= 0 implicit)
}

// -----------------------------------------------------------------------------
// VARIANT 5: Cast to unsigned for range check
// -----------------------------------------------------------------------------
const char* variant5_cast_unsigned(int tag) {
    if ((unsigned)tag > 30)
        return "(unknown)";
    return tag2str[tag];  // tag is 0..30
}

// -----------------------------------------------------------------------------
// VARIANT 6: Early return in else branch
// -----------------------------------------------------------------------------
const char* variant6_else_return(int tag) {
    if (tag >= 0 && tag <= 30) {
        return tag2str[tag];  // tag is 0..30
    } else {
        return "(unknown)";
    }
}

// -----------------------------------------------------------------------------
// VARIANT 7: Nested conditions
// -----------------------------------------------------------------------------
const char* variant7_nested(int tag) {
    if (tag >= 0) {
        if (tag <= 30) {
            return tag2str[tag];  // tag is 0..30
        }
    }
    return "(unknown)";
}

// -----------------------------------------------------------------------------
// VARIANT 8: Multiple arrays with same guard
// -----------------------------------------------------------------------------
const int values[31] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                        11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                        21, 22, 23, 24, 25, 26, 27, 28, 29, 30};

int variant8_multiple_arrays(int tag) {
    if (tag < 0 || tag > 30)
        return -1;
    const char* name = tag2str[tag];  // tag is 0..30
    int value = values[tag];           // tag is still 0..30
    return value + (name ? 1 : 0);
}

// -----------------------------------------------------------------------------
// VARIANT 9: Guard with function parameter bounds
// -----------------------------------------------------------------------------
char buffer[256];

char variant9_buffer_access(int n) {
    if (n < 0 || n >= 256)
        return '\0';
    return buffer[n];  // n is 0..255
}

// -----------------------------------------------------------------------------
// VARIANT 10: Guard followed by loop
// -----------------------------------------------------------------------------
int variant10_guard_before_loop(int start, int end) {
    if (start < 0 || end > 30 || start > end)
        return -1;
    int sum = 0;
    for (int i = start; i <= end; i++) {
        sum += values[i];  // i is in [start, end] which is in [0, 30]
    }
    return sum;
}

int main() {
    variant1_openssl_pattern(15);
    variant1_openssl_pattern(-1);
    variant1_openssl_pattern(50);

    variant2_reversed_order(10);
    variant3_gte_guard(5);
    variant4_unsigned_trick(20);
    variant5_cast_unsigned(25);
    variant6_else_return(0);
    variant7_nested(30);
    variant8_multiple_arrays(15);
    variant9_buffer_access(100);
    variant10_guard_before_loop(5, 10);

    return 0;
}
