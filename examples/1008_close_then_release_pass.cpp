// Example 1008: Close then release pattern
// Close via one pointer, then null the alias

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Close first, then release alias - should work
void close_then_release(bool cond) {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;  // g gets constraint, f is alias
    if (cond) {
        my_fclose(f);  // f.@open = 0, g.@open = 0 (via alias propagation)
        g = nullptr;   // g.@open = 0, constraint satisfied -> OK to release
    } else {
        my_fclose(g);  // g.@open = 0, f.@open = 0 (via alias propagation)
        f = nullptr;   // f.@open = 0 -> OK
    }
}

int main() {
    return 0;
}
