// 1742: Clamp idiom
//
// After `if (n < 0) n = 0;`, n >= 0 on both paths: 0 on one, the negated
// guard on the other. The join keeps the hull of the two ranges.

[[clang::annotate("dtt:requires=n >= 0")]] void take_size(int n);
[[clang::annotate("dtt:requires=n <= 10")]] void take_small(int n);

// Variant 1: clamp from below
void clamp(int n) {
    if (n < 0) n = 0;
    take_size(n);
}

// Variant 2: clamp from above
void clamp_above(int n) {
    if (n > 10) n = 10;
    take_small(n);
}

// Variant 3: clamp both ends
void clamp_both(int n) {
    if (n < 0) n = 0;
    if (n > 10) n = 10;
    take_size(n);
    take_small(n);
}

// Variant 4: a constant on each path
void pick(bool b) {
    int n = 3;
    if (b) n = 7;
    take_size(n);
    take_small(n);
}

int main() { return 0; }
