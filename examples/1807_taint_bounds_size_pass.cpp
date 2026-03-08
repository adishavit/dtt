// 1807_taint_bounds_size_pass.cpp
// Tests: Bounds checks on tainted data SHOULD suppress size-related sinks.
// For allocation sizes (CWE-190) and copy lengths (CWE-120), a bounds check
// establishes a value range that prevents integer overflow / buffer overflow.
// This is the principled Heartbleed fix pattern:
// proof system records _value<=N after guard, sink checker respects it.

extern "C" {
    char* getenv(const char*);
    int atoi(const char*);
    void* malloc(unsigned long);
    void free(void*);
}

// Variant 1: Simple bounds check suppresses malloc taint warning
// After if (sz > MAX) return, proof system has _value<=4096.
// malloc is CWE-190 (size sink) → upper bound proof is sufficient.
void test_bounded_alloc() {
    char* s = getenv("SIZE");
    if (!s) return;                    // null check for atoi
    int sz = atoi(s);                  // sz tainted via propagation
    if (sz > 4096) return;             // upper bound: _value<=4096
    if (sz < 0) return;               // lower bound: _value>=0
    void* p = malloc(sz);             // OK: sz bounded [0, 4096] for CWE-190
    free(p);
}

int main() { return 0; }
