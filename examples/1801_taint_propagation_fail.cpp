// 1801_taint_propagation_fail.cpp
// Tests: Taint propagates through assignments and function calls
// Tainted values reaching sinks produce errors

#include <cstdlib>
#include <cstring>

// Variant 1: Assignment propagation
void test_assign_propagation() {
    char* val = getenv("X");    // val is tainted
    char* copy = val;           // copy is tainted (assignment)
    system(copy);               // ERROR: tainted shell command
}

// Variant 2: strdup propagation (tainted string -> tainted copy)
void test_strdup_propagation() {
    char* val = getenv("X");    // val is tainted
    if (!val) return;           // null check for existing DTT
    char* dup = strdup(val);    // dup is tainted (strdup propagates)
    system(dup);                // ERROR: tainted shell command
    free(dup);
}

// Variant 3: Multiple assignment chain
void test_chain_propagation() {
    char* a = getenv("X");      // a is tainted
    char* b = a;                // b is tainted
    char* c = b;                // c is tainted
    system(c);                  // ERROR: tainted shell command
}

int main() { return 0; }
