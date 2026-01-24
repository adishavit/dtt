// 981_argc_symbolic_fail.cpp
// Tests: Symbolic size with unverified bounds - should FAIL (Category D warning)
// DTT now correctly warns when bounds cannot be proven safe

#include <vector>

int main(int argc, char* argv[]) {
    std::vector<int> v;
    v.resize(argc);    // _size = argc (symbolic)
    v[0] = 42;         // WARNING: can't prove 0 < argc
    v[1] = 42;         // WARNING: can't prove 1 < argc
    return 0;
}
