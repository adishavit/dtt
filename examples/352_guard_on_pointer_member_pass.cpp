// WHAT IS TESTED:
// Guards on pointer member fields propagate non-null proofs.
// Issue #44: FIXED - if (h.ptr != nullptr) now marks h.ptr non-null

struct Data {
    int value;
};

struct Holder {
    Data* ptr;
};

Data* getExternalData();  // Unknown nullability

// Variant 1: Basic pointer member null check
void variant1_basic(Holder h) {
    if (h.ptr != nullptr) {
        h.ptr->value = 1;
    }
}

// Variant 2: Equality check (ptr == nullptr) in else branch
void variant2_equality(Holder h) {
    if (h.ptr == nullptr) {
        return;
    }
    h.ptr->value = 1;
}

// Variant 3: Nested struct with pointer member
struct Outer {
    Holder holder;
};

void variant3_nested(Outer o) {
    if (o.holder.ptr != nullptr) {
        o.holder.ptr->value = 1;
    }
}

// Variant 4: Multiple pointer members
struct MultiHolder {
    Data* first;
    Data* second;
};

void variant4_multiple(MultiHolder m) {
    if (m.first != nullptr && m.second != nullptr) {
        m.first->value = 1;
        m.second->value = 2;
    }
}

int main() {
    Data d;
    Holder h{&d};
    variant1_basic(h);
    variant2_equality(h);

    Outer o{h};
    variant3_nested(o);

    MultiHolder m{&d, &d};
    variant4_multiple(m);

    return 0;
}
