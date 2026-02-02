// Example 1000b: Proper branch handling - all paths close
// Resource properly closed in all branches

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Variant 1: Close in both branches
void variant1_both_branches(bool flag) {
    FILE_handle* f = my_fopen("data.txt", "r");
    if (flag) {
        my_fclose(f);
    } else {
        my_fclose(f);
    }
}  // OK: closed in all paths

// Variant 2: Close before branch
void variant2_close_before_branch(bool flag) {
    FILE_handle* f = my_fopen("data.txt", "r");
    my_fclose(f);
    if (flag) {
        // Some other work
    }
}  // OK: closed before branching

// Variant 3: Early return with close
void variant3_early_return_closed(bool error) {
    FILE_handle* f = my_fopen("data.txt", "r");
    if (error) {
        my_fclose(f);
        return;
    }
    my_fclose(f);
}  // OK: closed on all paths

int main() {
    variant1_both_branches(true);
    variant2_close_before_branch(false);
    variant3_early_return_closed(false);
    return 0;
}
