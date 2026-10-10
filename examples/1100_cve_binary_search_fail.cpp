// CVE-like example: Binary search on unsorted data
// Real-world impact: Security vulnerabilities where attacker controls input order
//
// Similar to: CVE-2014-3566 (POODLE), CVE-2019-1040 (NTLM)
// These involved assumptions about data ordering that were violated.
//
// Value Concepts catches this by requiring 'sorted' precondition
// Main DTT cannot express "this array must be sorted"

#include <vector>
#include <algorithm>

// Binary search REQUIRES sorted input for correct behavior
// Without sorted input: may return wrong index, infinite loop, or miss existing elements
[[clang::annotate("dtt:requires=sorted")]]
int vulnerable_binary_search(const std::vector<int>& haystack, int needle) {
    size_t lo = 0, hi = haystack.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (haystack[mid] < needle)
            lo = mid + 1;
        else
            hi = mid;
    }
    return (lo < haystack.size() && haystack[lo] == needle) ? (int)lo : -1;
}

// Authentication bypass scenario:
// Attacker controls user_ids order, binary search may incorrectly
// return "not found" for a valid admin ID, or "found" for invalid ID
void check_admin_access() {
    // VULNERABLE: User-controlled data, not sorted
    std::vector<int> admin_ids = {1001, 500, 2000, 100, 999};

    int user_id = 500;  // Should be found at index 1
    int result = vulnerable_binary_search(admin_ids, user_id);  // ERROR: not sorted  dtt-error {{_sorted == 1}}

    // Binary search on unsorted data may return -1 (not found)
    // even though 500 IS in the array, causing auth bypass
}

int main() {
    check_admin_access();
    return 0;
}
