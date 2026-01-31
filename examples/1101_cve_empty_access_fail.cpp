// CVE-like example: Accessing empty container
// Real-world impact: Crashes, denial of service, potential code execution
//
// Similar to: CVE-2022-24070 (Apache APISIX), CVE-2021-44228 (Log4Shell patterns)
// Empty input vectors causing unexpected behavior in critical paths
//
// Value Concepts catches this via 'nonempty' precondition
// Main DTT tracks _size but can't express "must not be empty"

#include <vector>
#include <string>

// Process REQUIRES at least one element
// Empty vector causes .front()/.back() undefined behavior
[[clang::annotate("dtt:requires=nonempty")]]
int get_priority_message(const std::vector<int>& priority_queue) {
    return priority_queue.front();  // UB if empty
}

// Security-critical: parsing authentication tokens
[[clang::annotate("dtt:requires=nonempty")]]
std::string extract_first_claim(const std::vector<std::string>& claims) {
    return claims.front();  // UB if empty - crash or memory corruption
}

void process_auth_request() {
    // Attacker sends request with empty claims array
    std::vector<std::string> malicious_claims;  // Empty!

    // VULNERABLE: Crashes or reads garbage memory
    std::string claim = extract_first_claim(malicious_claims);  // ERROR: not nonempty
    // Could leak memory contents or crash auth service
}

void process_batch_jobs() {
    // Race condition: another thread cleared the queue
    std::vector<int> job_queue;  // Empty after race

    // VULNERABLE: UB access
    int next_job = get_priority_message(job_queue);  // ERROR: not nonempty
}

int main() {
    process_auth_request();
    process_batch_jobs();
    return 0;
}
