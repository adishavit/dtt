// Example 156: Mixed const/non-const params — should FAIL
//
// When a function has multiple parameters, only non-const refs should
// trigger invalidation. Const refs should preserve properties.

struct Vec {
    int* data;
    int _size;
};

// Mixed params: src is const (read-only), dst is non-const (modified)
void copy_some(const Vec& src, Vec& dst);

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Vec& v);

void test() {
    Vec src, dst;
    src._size = 10;
    dst._size = 5;

    copy_some(src, dst);  // src preserved (const), dst invalidated

    process(src);  // Should PASS: src._size still 10 (was const ref)
    process(dst);  // Should FAIL: dst._size unknown (was non-const ref)
}

int main() { test(); return 0; }
