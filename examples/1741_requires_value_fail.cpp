// 1741: Argument-value preconditions, attribute and .dtt forms alike
//
// take_count's precondition comes from annotations/test_custom.dtt
// (requires arg0 >= 0), take_size's from a dtt:requires attribute. Both
// forms must give the same verdict at each call: a violated value is an
// error, and a value DTT cannot establish is "cannot verify" (an error in
// enforce mode).

#include <vector>

void take_count(int n);
void take_items(const std::vector<int> &v);
[[clang::annotate("dtt:requires=n >= 0")]] void take_size(int n);
int read_value();

// Variant 1: a constant that violates the precondition.
void variant1_constant() {
    take_count(-8);  // dtt-error {{requires arg0 >= 0}}
    take_size(-8);   // dtt-error {{requires arg0 >= 0}}
}

// Variant 2: a variable assigned a violating constant.
void variant2_assigned(int n) {
    n = -5;
    take_count(n);  // dtt-error {{requires arg0 >= 0}}
    take_size(n);   // dtt-error {{requires arg0 >= 0}}
}

// Variant 3: an unknown value.
void variant3_unknown(int n) {
    take_count(n);  // dtt-error {{cannot verify}}
    take_size(n);   // dtt-error {{cannot verify}}
}

// Variant 4: a guard that does not imply the precondition (n >= -10).
void variant4_weak_guard(int n) {
    if (n < -10) return;
    take_count(n);  // dtt-error {{cannot verify}}
    take_size(n);   // dtt-error {{cannot verify}}
}

// Variant 5: a guard invalidated by a later write.
void variant5_guard_then_write(int n) {
    if (n < 0) return;
    n = read_value();
    take_count(n);  // dtt-error {{cannot verify}}
    take_size(n);   // dtt-error {{cannot verify}}
}

// Variant 6: a requirement propagated through a static wrapper is reported
// with its own operator.
static void forward_items(const std::vector<int> &v) { take_items(v); }
void variant6_propagated(const std::vector<int> &v) {
    forward_items(v);  // dtt-error {{cannot verify _size > 0}}
}

int main() { return 0; }
