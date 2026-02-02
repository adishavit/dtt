// CVE fixes: Demonstrating proper use of Value Concepts
// These are the FIXED versions of the vulnerable patterns
//
// Shows how Value Concepts guide developers to write safe code

#include <vector>
#include <algorithm>

// Requires sorted input
[[clang::annotate("dtt:requires=sorted")]]
int safe_binary_search(const std::vector<int>& haystack, int needle) {
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

// Sort establishes 'sorted' postcondition
[[clang::annotate("dtt:ensures=sorted")]]
void prepare_for_search(std::vector<int>& v) {
    std::sort(v.begin(), v.end());
}

// Requires non-empty
[[clang::annotate("dtt:requires=nonempty")]]
int get_first_element(const std::vector<int>& v) {
    return v.front();
}

// FIXED: Sort before binary search
void fixed_check_admin_access() {
    std::vector<int> admin_ids = {1001, 500, 2000, 100, 999};

    // FIX: Sort first to establish 'sorted'
    prepare_for_search(admin_ids);

    int user_id = 500;
    int result = safe_binary_search(admin_ids, user_id);  // OK: sorted
}

// FIXED: Check non-empty before access
void fixed_process_jobs() {
    std::vector<int> job_queue = {42};  // Non-empty

    // OK: queue is non-empty
    int job = get_first_element(job_queue);  // OK: nonempty
}

int main() {
    fixed_check_admin_access();
    fixed_process_jobs();
    return 0;
}
