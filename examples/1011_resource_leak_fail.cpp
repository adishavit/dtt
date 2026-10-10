// Example 1011: Resource leaks - should be detected
// Tests that DTT catches leaks even with aliases

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Variant 1: All aliases released, none close - LEAK
void variant1_all_released_no_close() {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;
    f = nullptr;  // Release
    g = nullptr;  // Release - ERROR: resource leaked, no handle remains  dtt-error {{leaks resource}}
}

// Variant 2: Last handle goes out of scope without close - LEAK
void variant2_scope_exit_leak() {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;
    f = nullptr;
    // g goes out of scope without close - ERROR: resource leaked
}

// Variant 3: Double close via different aliases - ERROR
void variant3_double_close() {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;
    my_fclose(f);  // Close resource
    my_fclose(g);  // ERROR: resource already closed  dtt-error {{arg0\.@open == 1}}
}

int main() {
    return 0;
}
