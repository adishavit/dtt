// 984_main_argc_pass.cpp
// Tests: main's first int parameter is guaranteed >= 1 - should PASS
// Per C++ standard, argc is always at least 1 (program name in argv[0])

#include <vector>

// Variant 1: Standard argc naming
int main(int argc, char* argv[]) {
    std::vector<int> v;
    v.resize(argc);
    v[0] = 42;  // OK: argc >= 1 guaranteed
    return 0;
}
