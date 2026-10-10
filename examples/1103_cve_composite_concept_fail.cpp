// CVE-like example: Composite security requirements
// Real-world impact: Security bypass when multiple conditions required
//
// Similar to: CVE-2023-44487 (HTTP/2 Rapid Reset), CVE-2022-22965 (Spring4Shell)
// These involved complex state requirements that weren't properly verified
//
// Value Concepts allows user-defined composite concepts
// Main DTT cannot combine multiple semantic requirements

#include <vector>
#include <algorithm>

// Define composite concept: validated_sorted := nonempty && sorted
// Cryptographic operations require both non-empty AND sorted for timing-safe compare
[[clang::annotate("dtt:define=validated_sorted := nonempty && sorted")]]

// Timing-safe comparison requires:
// 1. nonempty - can't compare empty sequences
// 2. sorted - binary search used internally for O(log n)
[[clang::annotate("dtt:requires=validated_sorted")]]
bool timing_safe_contains(const std::vector<unsigned char>& whitelist,
                          unsigned char token) {
    // Implementation assumes non-empty and sorted for correctness
    auto it = std::lower_bound(whitelist.begin(), whitelist.end(), token);
    return it != whitelist.end() && *it == token;
}

// Define: authenticated_buffer := nonempty && nonnull
[[clang::annotate("dtt:define=ready_queue := nonempty && sorted")]]

[[clang::annotate("dtt:requires=ready_queue")]]
int get_highest_priority(const std::vector<int>& queue) {
    return queue.back();  // Sorted: highest is last
}

void verify_auth_token() {
    // Security bypass: whitelist is empty due to config error
    std::vector<unsigned char> allowed_tokens;  // Empty!

    unsigned char user_token = 0x42;
    // VULNERABLE: Empty whitelist means timing_safe_contains crashes
    bool allowed = timing_safe_contains(allowed_tokens, user_token);  // ERROR  dtt-error {{_size > 0 \&\& _sorted == 1}}
}

void schedule_task() {
    // Race condition: queue cleared between check and use
    std::vector<int> task_queue = {3, 1, 5, 2};  // Not sorted!

    // VULNERABLE: Not sorted, get_highest_priority returns wrong value
    int top = get_highest_priority(task_queue);  // ERROR: not sorted  dtt-error {{_size > 0 \&\& _sorted == 1}}
    // May schedule wrong task - potential privilege escalation
}

int main() {
    verify_auth_token();
    schedule_task();
    return 0;
}
