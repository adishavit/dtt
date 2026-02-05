// 1501: strtok internal state across calls - DTT gap
// strtok stores a pointer internally; if the original string is freed between
// calls, subsequent strtok(NULL, ...) invocations access freed memory.
// DTT cannot track cross-call internal state.

#include <cstdlib>
#include <cstring>

// Variant 1: free string between strtok calls
void variant1_strtok_cross_call_uaf() {
    char* str = (char*)malloc(100);
    if (!str) return;
    strcpy(str, "hello,world,foo");

    char* tok = strtok(str, ",");  // Returns "hello", stores internal pointer
    (void)tok;

    free(str);  // Free the string strtok is internally pointing to

    tok = strtok(nullptr, ",");  // BUG: use-after-free via strtok internal state
    (void)tok;
}

// Variant 2: realloc between strtok calls
void variant2_strtok_realloc() {
    char* str = (char*)malloc(50);
    if (!str) return;
    strcpy(str, "a,b,c,d");

    char* tok = strtok(str, ",");
    (void)tok;

    // realloc may move the buffer, invalidating strtok's internal pointer
    char* new_str = (char*)realloc(str, 200);
    if (new_str) str = new_str;

    tok = strtok(nullptr, ",");  // BUG: internal pointer may be stale
    (void)tok;
    free(str);
}

int main() { return 0; }
