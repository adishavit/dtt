// Example 1003: Null check before early return - no leak
// If open fails (returns NULL), no resource to release

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Null check then early return - OK
// After `if (!f) return;`, f is NULL in that path - no constraint applies
int null_check_return() {
    FILE_handle* f = my_fopen("data.txt", "r");
    if (!f) return -1;  // OK: f is NULL, no resource acquired
    my_fclose(f);
    return 0;
}

// Alternative: explicit NULL comparison
int null_check_explicit() {
    FILE_handle* f = my_fopen("data.txt", "r");
    if (f == nullptr) return -1;  // OK: f is NULL
    my_fclose(f);
    return 0;
}

int main() {
    null_check_return();
    null_check_explicit();
    return 0;
}
