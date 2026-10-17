// 1764: A function's contract belongs to that function (safe uses)
//
// These run in inform mode: "cannot verify" is expected where nothing is
// known; a proven violation from another function's contract is not.

struct Vec {
    int *data;
    int len;
};
[[clang::annotate("dtt:requires=len == 0")]] void needsEmpty(Vec &v);

namespace a {
[[clang::annotate("dtt:ensures=arg0.len == 10")]] void fill(Vec &v);
}
namespace b {
void fill(Vec &v);
}
[[clang::annotate("dtt:ensures=arg0.len == 10")]] void fill(Vec &v);
void fill(Vec &v, int n);  // overload, no contract
struct W {
    void fill(Vec &v);  // method, no contract
};

// Variant 1: another namespace's function.
void variant1_other_namespace() {
    Vec v{nullptr, 0};
    b::fill(v);
    needsEmpty(v);
}

// Variant 2: an overload.
void variant2_overload() {
    Vec v{nullptr, 0};
    fill(v, 3);
    needsEmpty(v);
}

// Variant 3: a method of the same name.
void variant3_method() {
    Vec v{nullptr, 0};
    W w;
    w.fill(v);
    needsEmpty(v);
}

// Variant 4: an overload whose sibling dereferences its parameter.
static void use(int *p) { *p = 1; }
static void use(int *p, int n) {
    if (p) *p = n;
}
void variant4_static_overload() {
    use(nullptr, 0);
    int x;
    use(&x);
}

// Variant 5: total on another namespace's function.
enum Color { Red, Green, Blue };
namespace a {
[[clang::annotate("dtt:total")]] int code(Color c);
}
namespace b {
int code(Color c) {
    switch (c) {
    case Red: return 1;
    case Green: return 2;
    }
    return 0;
}
}  // namespace b

// Variant 6: a user function named like a C library function.
namespace pool {
char *strdup(const char *s);
}
void variant6_library_name() {
    char *s = pool::strdup("x");
    if (s) s[0] = 'a';
}

// Variant 7: a variadic overload.
[[clang::annotate("dtt:ensures=arg0.len == 10")]] void vfill(Vec &v);
void vfill(Vec &v, ...);
void variant7_variadic() {
    Vec v{nullptr, 0};
    vfill(v, 1);
    needsEmpty(v);
}

// Variant 8: a static function named like a C library function.
static int open(const char *, int f) { return f; }
int variant8_static_libc_name() {
    int fd = open("x", 0);
    if (fd >= 0) return 1;
    return 0;
}

int main() { return 0; }
