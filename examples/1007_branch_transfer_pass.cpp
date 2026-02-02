// Example 1007: Ownership transfer on different branches
// Explicit release before close on each path

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Explicit ownership transfer before close - should be OK
void transfer_before_close(bool cond) {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;  // g aliases f, constraint transfers to g
    if (cond) {
        g = nullptr;   // release g's claim (but g has no constraint - it moved to f? or stayed?)
        my_fclose(f);  // close via f
    } else {
        f = nullptr;   // release f's claim
        my_fclose(g);  // close via g
    }
}  // OK: closed on both paths

int main() {
    return 0;
}
