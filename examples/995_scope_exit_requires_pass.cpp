// Example 995: Scope-exit requires (~requires) - should pass (no DTT error)
// Linear resources properly consumed before scope exit
//
// Tests that properly consumed resources satisfy ~requires constraints.
// Note: Path-sensitive variants (branches, conditional open) are in 996_scope_exit_path_xfail.cpp

// Simulated C-style file API with scope-exit constraint
struct FILE_handle {};

// fopen: returns handle with @open == 1, requires @open == 0 at scope exit
[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

// fclose: transitions @open from 1 to 0
// Note: Using void return to ensure postcondition applies to arg0
[[clang::annotate("dtt:requires=@open == 1")]]
[[clang::annotate("dtt:ensures=@open == 0")]]
void my_fclose(FILE_handle* f);

// Variant 1: Simple correct usage
void variant1_simple_correct() {
    FILE_handle* f = my_fopen("data.txt", "r");
    my_fclose(f);
}  // OK: @open == 0 at scope exit

// Variant 2: Multiple resources, all cleaned up
void variant2_full_cleanup() {
    FILE_handle* f1 = my_fopen("file1.txt", "r");
    FILE_handle* f2 = my_fopen("file2.txt", "r");
    my_fclose(f1);
    my_fclose(f2);
}  // OK: both closed

int main() {
    variant1_simple_correct();
    variant2_full_cleanup();
    return 0;
}
