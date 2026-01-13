// Tests: Pointer alias soundness - reassignment breaks alias relationship
// This tests that DTT doesn't unsoundly propagate proofs after reassignment

struct Data {
    int value;
};

Data* getExternalData();  // Unknown nullability

// Variant 1: Alias reassigned to different pointer - should NOT inherit proof
void variant1_alias_reassigned(Data* ptr, Data* other) {
    Data* alias = ptr;
    alias = other;  // Reassignment breaks alias relationship
    if (ptr != nullptr) {
        alias->value = 1;  // BUG: alias is now 'other', not 'ptr'
    }
}

// Variant 2: Original pointer reassigned - alias still points to old value
void variant2_original_reassigned(Data* ptr, Data* other) {
    Data* alias = ptr;
    ptr = other;  // Original reassigned
    if (ptr != nullptr) {
        // ptr is now 'other' which is non-null
        // but alias still points to original 'ptr' value
        alias->value = 1;  // BUG: alias's nullability is unknown
    }
}

int main() {
    variant1_alias_reassigned(getExternalData(), getExternalData());
    variant2_original_reassigned(getExternalData(), getExternalData());
    return 0;
}
