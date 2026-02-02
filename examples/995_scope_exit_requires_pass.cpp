// Example 995: Scope-exit requires (~requires) - should pass (no DTT error)
// Linear resources properly consumed before scope exit
//
// Tests that properly consumed resources satisfy ~requires constraints.

// Simulated C-style file API with scope-exit constraint
struct FILE_handle {};

// fopen: returns handle with @open == 1, requires @open == 0 at scope exit
[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

// fclose: transitions @open from 1 to 0
[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Variant 1: Simple correct usage
void variant1_simple_correct() {
    FILE_handle* f = my_fopen("data.txt", "r");
    my_fclose(f);
}  // OK: @open == 0 at scope exit

// Variant 3: Multiple resources, all cleaned up
void variant3_full_cleanup() {
    FILE_handle* f1 = my_fopen("file1.txt", "r");
    FILE_handle* f2 = my_fopen("file2.txt", "r");
    my_fclose(f1);
    my_fclose(f2);
}  // OK: both closed

// Variant 4: Nested scope with cleanup
void variant4_nested_cleanup() {
    {
        FILE_handle* f = my_fopen("data.txt", "r");
        my_fclose(f);
    }  // OK: f closed before scope exit
}

// Path-sensitive variants (known limitations, moved to xfail):
// - variant2: if/else branch cleanup - requires path-sensitive analysis
// - variant5: conditional open with conditional close - requires null tracking

int main() {
    variant1_simple_correct();
    return 0;
}
