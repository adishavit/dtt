// CVE-like example: Array element value violations
// Real-world impact: Integer overflow, negative array index, division by zero
//
// Similar to: CVE-2021-21300 (Git), CVE-2020-8617 (BIND DNS)
// Array contents used in calculations without value validation
//
// Value Concepts + forall catches element-level constraints
// Main DTT cannot express "all elements must be positive"

#include <cstddef>

// Safe division requires all denominators > 0
// forall(i, 0, n, arr[i] > 0)
[[clang::annotate("dtt:requires=forall(i, 0, n, arr[i] > 0)")]]
void batch_divide(int* results, const int* denominators, size_t n, int numerator) {
    for (size_t i = 0; i < n; ++i) {
        results[i] = numerator / denominators[i];  // Safe: all > 0
    }
}

// Array index calculation: elements must be non-negative
[[clang::annotate("dtt:requires=forall(i, 0, n, offsets[i] >= 0)")]]
void lookup_with_offsets(char* dest, const char* src, const int* offsets, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        dest[i] = src[offsets[i]];  // Safe: no negative indices
    }
}

void process_user_rates() {
    // User-controlled rate limits, could contain zero or negative
    int rates[] = {100, 50, 0, 25};  // Contains zero!
    int results[4];

    // VULNERABLE: Division by zero at index 2
    batch_divide(results, rates, 4, 1000);  // ERROR: forall violation  dtt-error {{forall\(i, 0, arg2, arr\[i\] > 0\)}}
}

void process_network_offsets() {
    // Packet contains attacker-controlled offsets
    int malicious_offsets[] = {0, 5, -3, 10};  // Contains negative!
    char buffer[100];
    char dest[4];

    // VULNERABLE: Negative array index at position 2
    lookup_with_offsets(dest, buffer, malicious_offsets, 4);  // ERROR: forall violation  dtt-error {{forall\(i, 0, arg3, arg2\[i\] >= 0\)}}
}

int main() {
    process_user_rates();
    process_network_offsets();
    return 0;
}
