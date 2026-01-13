// Tests: Pointer member guard soundness - reassignment invalidates proof
// This ensures DTT doesn't unsoundly propagate proofs after member reassignment

struct Data {
    int value;
};

struct Holder {
    Data* ptr;
};

Data* getExternalData();  // Unknown nullability

// Variant 1: Member reassigned after guard - should NOT inherit proof
void variant1_reassigned_after_guard(Holder h) {
    if (h.ptr != nullptr) {
        h.ptr = getExternalData();  // Reassignment invalidates proof
        h.ptr->value = 1;           // BUG: h.ptr nullability is now unknown
    }
}

// Variant 2: Guard on different member - should NOT transfer
void variant2_different_member() {
    Holder h;
    h.ptr = getExternalData();
    Data d;
    Data* other = &d;

    if (other != nullptr) {
        h.ptr->value = 1;  // BUG: guard was on 'other', not h.ptr
    }
}

int main() {
    Holder h;
    h.ptr = getExternalData();
    variant1_reassigned_after_guard(h);
    variant2_different_member();
    return 0;
}
