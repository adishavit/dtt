// 1754: switch and reachability - facts on each path are proven
//
// Runs in enforce mode, so "cannot verify" fails the test.
#include <cstdlib>
#include <variant>

int a[4];

// Variant 1: inside `case 3`, k is 3.
int variant1_case_value(int k) {
    switch (k) {
    case 3:
        return a[k];
    default:
        return 0;
    }
}

// Variant 2: a range case bounds k.
int variant2_case_range(int k) {
    switch (k) {
    case 0 ... 3:
        return a[k];
    default:
        return 0;
    }
}

// Variant 3: the default path knows k is none of the cases.
int variant3_default_excludes_cases(unsigned k) {
    if (k > 4) return 0;
    switch (k) {
    case 4:
        return 0;
    default:
        return a[k];
    }
}

// Variant 4: a null check whose failing branch aborts.
int variant4_abort_guard(int *p) {
    if (!p) abort();
    return *p;
}

// Variant 5: each case of a switch on v.index() knows the alternative.
double variant5_variant_index(const std::variant<int, double> &v) {
    switch (v.index()) {
    case 0:
        return std::get<0>(v);
    case 1:
        return std::get<1>(v);
    default:
        return 0;
    }
}

// Variant 6: inside a case, the condition has the case value, even where
// an earlier value disagreed.
int variant6_case_value_replaces_known_value() {
    int k = 2;
    switch (k) {
    case 3:
        return a[k];
    default:
        return a[k];
    }
}

// Variant 7: a guard whose failing branch is unreachable bounds n.
void variant7_unreachable_bound(unsigned long n) {
    int arr[100] = {0};
    if (n > 100) __builtin_unreachable();
    for (unsigned long i = 0; i < n; i++) arr[i] = 1;
}

int main() { return 0; }
