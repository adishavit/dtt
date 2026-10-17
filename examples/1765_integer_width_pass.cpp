// 1765: Integer values have the width and signedness of their type (safe uses)
//
// A value that wraps or narrows into range is in range: DTT took these at
// their mathematical value and reported indices and arguments that are fine.

#include <cstdlib>
#include <vector>

[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);

// Variant 1: unsigned char 300 is 44.
int variant1_narrow_index() {
    std::vector<int> v(50);
    unsigned char c = 300;
    return v[c];
}

// Variant 2: unsigned char 250 + 10 stored back is 4.
int variant2_narrow_wrap() {
    std::vector<int> v(256);
    unsigned char c = 250;
    c = c + 10;
    return v[c];
}

// Variant 3: UINT_MAX + 1 is 0.
int variant3_unsigned_wrap_to_zero() {
    std::vector<int> v(10);
    unsigned u = 4294967295u;
    unsigned w = u + 1;
    return v[w];
}

// Variant 4: ++ on unsigned char 255 is 0.
int variant4_increment_wraps() {
    std::vector<int> v(10);
    unsigned char c = 255;
    ++c;
    return v[c];
}

// Variant 5: unsigned constants keep their value.
void variant5_unsigned_constants() {
    take((unsigned char)200);
    take((unsigned short)40000);
    take((unsigned char)-1);
}

// Variant 6: an allocation sized by an unsigned constant.
void variant6_unsigned_allocation() {
    char *p = (char *)malloc(2147483648u);
    if (p) {
        p[10] = 0;
        free(p);
    }
    char *q = (char *)malloc((unsigned char)200);
    if (q) {
        q[10] = 0;
        free(q);
    }
}

// Variant 7: unsigned arithmetic that cannot wrap keeps its value.
int variant7_no_wrap(unsigned n) {
    std::vector<int> v(10);
    if (n >= 1 && n <= 10) return v[n - 1];
    return 0;
}

// Variant 8: a loop index plus one below a bound.
int variant8_index_plus_one(const std::vector<int> &v) {
    int s = 0;
    for (std::size_t i = 0; i + 1 < v.size(); ++i) s += v[i + 1];
    return s;
}

int main() { return 0; }
