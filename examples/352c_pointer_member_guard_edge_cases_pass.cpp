// Tests: Edge cases for pointer member guards
// These should all PASS - DTT should recognize various guard patterns

struct Data {
    int value;
};

struct Holder {
    Data* ptr;
    Data** ptrptr;
};

Data* getExternalData();

// Variant 1: Reversed comparison (nullptr on LHS)
void variant1_reversed(Holder h) {
    if (nullptr != h.ptr) {
        h.ptr->value = 1;
    }
}

// Variant 2: Using 0 instead of nullptr
void variant2_zero(Holder h) {
    if (h.ptr != 0) {
        h.ptr->value = 1;
    }
}

// Variant 3: Guard in method using this->ptr
struct Container {
    Data* ptr;

    void process() {
        if (this->ptr != nullptr) {
            this->ptr->value = 1;
        }
    }

    void processImplicit() {
        if (ptr != nullptr) {
            ptr->value = 1;
        }
    }
};

// Variant 4: Double negation
void variant4_double_negation(Holder h) {
    if (!(h.ptr == nullptr)) {
        h.ptr->value = 1;
    }
}

int main() {
    Data d;
    Holder h{&d, nullptr};
    variant1_reversed(h);
    variant2_zero(h);
    variant4_double_negation(h);

    Container c{&d};
    c.process();
    c.processImplicit();

    return 0;
}
