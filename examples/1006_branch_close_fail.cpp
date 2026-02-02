// Example 1006: Double close on different branches
// Tests path-sensitive analysis for resource tracking

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Variant 1: Close in branch, then close after - DOUBLE CLOSE on one path
void variant1_branch_then_close(bool cond) {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;
    if (cond) {
        my_fclose(f);  // On this path: f.@open = 0, g.@open = 0
    }
    my_fclose(g);  // ERROR if cond: g.@open = 0
}                  // ERROR if !cond: g not closed (but f.@open = 1)

// Variant 2: Close in both branches - OK
void variant2_close_both_branches(bool cond) {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;
    if (cond) {
        my_fclose(f);
    } else {
        my_fclose(g);
    }
}  // OK: closed on both paths

// Variant 3: Close only in one branch - LEAK on other path
void variant3_close_one_branch(bool cond) {
    FILE_handle* f = my_fopen("data.txt", "r");
    if (cond) {
        my_fclose(f);
    }
}  // ERROR if !cond: f not closed

int main() {
    return 0;
}
