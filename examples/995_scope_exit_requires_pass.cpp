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
// Note: Using void return to ensure postcondition applies to arg0
[[clang::annotate("dtt:requires=@open == 1")]]
[[clang::annotate("dtt:ensures=@open == 0")]]
void my_fclose(FILE_handle* f);

// Variant 1: Simple correct usage
void variant1_simple_correct() {
    FILE_handle* f = my_fopen("data.txt", "r");
    my_fclose(f);
}  // OK: @open == 0 at scope exit

// Variant 2: Correct cleanup on both branches
void variant2_both_branches(bool error) {
    FILE_handle* f = my_fopen("data.txt", "r");
    if (error) {
        my_fclose(f);
        return;
    }
    my_fclose(f);
}  // OK: closed on all paths

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

// Variant 5: Conditional open with conditional close
void variant5_conditional_open(bool need_file) {
    FILE_handle* f = nullptr;
    if (need_file) {
        f = my_fopen("data.txt", "r");
    }
    // ... use f ...
    if (f) {
        my_fclose(f);
    }
}  // OK: f is either null or closed

int main() {
    variant1_simple_correct();
    return 0;
}
