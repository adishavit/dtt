// 1016_early_return_proof_pass.cpp
// Tests: Proof preservation after early return in nested if
// DTT correctly preserves proofs when inner if-block terminates via return

#include <cstdio>

// Variant 1: Simple early return - proof should survive
void test_early_return_simple() {
    FILE* fp = fopen("file.txt", "r");
    if (fp) {
        if (feof(fp)) {
            fclose(fp);
            return;  // Early return - only this path has _nonnull=0
        }
        // Fall-through: fp is still valid, _nonnull proof should hold
        fclose(fp);  // Should be OK - fp proven non-null from outer if
    }
}

// Variant 2: Multiple early returns
void test_multiple_early_returns() {
    FILE* fp = fopen("file.txt", "r");
    if (fp) {
        if (feof(fp)) {
            fclose(fp);
            return;
        }
        if (ferror(fp)) {  // Should have _nonnull proof
            fclose(fp);
            return;
        }
        // Both early returns taken - fall through still valid
        fclose(fp);  // Should be OK
    }
}

int main() { return 0; }
