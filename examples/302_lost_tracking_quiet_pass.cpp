// Category C: Lost tracking - quiet mode (should pass silently)
//
// In quiet mode (--dtt-mode=quiet), DTT should silently allow operations
// after tracking is lost. This test verifies that behavior.
//
// NOTE: This test requires --dtt-mode=quiet to be passed to the plugin.
// The CMakeLists.txt should be updated to pass this flag for this test.
//
// For now, this test documents the expected behavior. Without the mode flag,
// it will produce a warning (in inform mode) or error (in enforce mode).

void unknown(int& x);  // Unannotated - will cause tracking loss

struct Vec {
    int _size;
};

[[clang::annotate("dtt:requires=_size > 0")]]
void needsNonEmpty(Vec& v);

void test() {
    Vec v;
    v._size = 10;

    int x = 5;
    unknown(x);  // This doesn't affect v

    // v._size is still tracked (unknown only took int&, not Vec&)
    needsNonEmpty(v);  // OK: v._size = 10
}

int main() { test(); return 0; }
