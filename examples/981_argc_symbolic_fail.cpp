// 981_argc_symbolic_fail.cpp
// Tests: Symbolic size with unverified bounds - should FAIL (Category D warning)
// Note: v[0] now passes because main's argc >= 1 is guaranteed

#include <vector>

int main(int argc, char* argv[]) {
    std::vector<int> v;
    v.resize(argc);    // _size = argc (symbolic, but >= 1)
    v[0] = 42;         // OK: argc >= 1 guaranteed by C++ standard
    v[1] = 42;         // WARNING: can't prove 1 < argc (needs argc >= 2)
    return 0;
}
