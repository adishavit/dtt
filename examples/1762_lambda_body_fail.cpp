// 1762: Lambda bodies are analyzed
//
// DTT skipped lambda bodies entirely, so nothing in them was checked.
#include <vector>

[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);

// Variant 1: a lambda at namespace scope.
auto global_lambda = []() {
    int a[2];
    a[5] = 1;  // dtt-error {{out of bounds}}
};

// Variant 2: a local lambda, called.
void variant2_local() {
    auto l = []() {
        int a[2];
        a[5] = 1;  // dtt-error {{out of bounds}}
    };
    l();
}

// Variant 3: an empty container, a null pointer, a violated requirement.
void variant3_kinds() {
    auto v = []() {
        std::vector<int> e;
        return e[0];  // dtt-error {{out of bounds|_size}}
    };
    auto p = []() {
        int *q = nullptr;
        *q = 1;  // dtt-error {{null}}
    };
    auto t = []() { take(-1); };  // dtt-error {{arg0 >= 0}}
    v();
    p();
    t();
}

// Variant 4: a captured value is not known inside the body.
void variant4_capture_copy() {
    int n = -1;
    auto l = [n]() { take(n); };  // dtt-error {{arg0 >= 0|cannot verify}}
    l();
}

// Variant 5: a lambda parameter is not known inside the body.
void variant5_parameter() {
    auto l = [](int k) { take(k); };  // dtt-error {{cannot verify}}
    l(3);
}

int main() { return 0; }
