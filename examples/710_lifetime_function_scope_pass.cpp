// 710_lifetime_function_scope_pass.cpp
// Tests that pointer lifetime tracking is properly scoped per function
// Bug: DTT was incorrectly detecting double-free across different functions
// because PointerLifetimes map was not cleared at function boundaries

#include <cstdlib>

// Function 1: allocates and frees 'buf'
void function1() {
    char* buf = (char*)malloc(100);
    if (!buf) return;
    buf[0] = 'a';
    free(buf);  // First free of 'buf' in function1
}

// Function 2: also allocates and frees 'buf' - DIFFERENT variable!
void function2() {
    char* buf = (char*)malloc(200);
    if (!buf) return;
    buf[0] = 'b';
    free(buf);  // This is NOT a double-free - it's a different 'buf'!
}

// Function 3: same pattern with 'ptr'
void function3_alloc() {
    int* ptr = new int[10];
    ptr[0] = 42;
    delete[] ptr;
}

void function3_use() {
    int* ptr = new int[20];  // Different 'ptr'
    ptr[0] = 99;
    delete[] ptr;  // NOT a double-free
}

// Function 4: mixing malloc/new across functions
void function4_malloc() {
    void* p = malloc(50);
    if (p) free(p);
}

void function4_new() {
    int* p = new int;  // Different 'p', different allocation type
    *p = 123;
    delete p;  // NOT a mismatch - this 'p' was allocated with new
}

// Function 5: reusing common names like 'result', 'temp', 'data'
char* function5_helper() {
    char* result = (char*)malloc(10);
    return result;  // Caller takes ownership
}

void function5_caller() {
    char* result = function5_helper();  // Different 'result' variable
    if (result) {
        result[0] = 'x';
        free(result);
    }
}

// Function 6: nested scopes within same function (should still work)
void function6_nested() {
    {
        char* buf = (char*)malloc(100);
        if (buf) free(buf);
    }
    {
        char* buf = (char*)malloc(200);  // Same name, new scope
        if (buf) free(buf);  // Should be OK - different allocation
    }
}

int main() {
    function1();
    function2();
    function3_alloc();
    function3_use();
    function4_malloc();
    function4_new();
    function5_caller();
    function6_nested();
    return 0;
}
