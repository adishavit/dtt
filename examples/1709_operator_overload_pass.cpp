// 1709: Operator overloads must not crash DTT
// DTT calls getName() on declarations without checking getIdentifier() first.
// getName() asserts on operators, constructors, destructors, and conversion functions.
// Found by scanning bitcoin/bitcoin — 59 out of 248 files crashed DTT.

#include <cstddef>

// Variant 1: operator[] on custom container (bitcoin prevector/CScript pattern)
struct ByteVec {
    unsigned char data[256];
    size_t sz;

    unsigned char& operator[](size_t i) { return data[i]; }
    const unsigned char& operator[](size_t i) const { return data[i]; }
    size_t size() const { return sz; }
};

void variant1_subscript_operator() {
    ByteVec v;
    v.sz = 10;
    v[0] = 42;
}

// Variant 2: operator== and operator!= (bitcoin uint256 pattern)
struct Hash256 {
    unsigned char data[32];

    bool operator==(const Hash256& other) const {
        for (int i = 0; i < 32; i++)
            if (data[i] != other.data[i]) return false;
        return true;
    }
    bool operator!=(const Hash256& other) const { return !(*this == other); }
};

void variant2_comparison_operators() {
    Hash256 a{}, b{};
    bool eq = (a == b);
    bool ne = (a != b);
    (void)eq; (void)ne;
}

// Variant 3: operator+= and arithmetic (bitcoin arith_uint256 pattern)
struct BigInt {
    unsigned long long parts[4];

    BigInt& operator+=(const BigInt& other) {
        for (int i = 0; i < 4; i++) parts[i] += other.parts[i];
        return *this;
    }
    BigInt operator+(const BigInt& other) const {
        BigInt result = *this;
        result += other;
        return result;
    }
    BigInt& operator<<=(int shift) {
        (void)shift;
        return *this;
    }
};

void variant3_arithmetic_operators() {
    BigInt a{}, b{};
    BigInt c = a + b;
    c += a;
    c <<= 1;
    (void)c;
}

// Variant 4: conversion operator (implicit bool, explicit int)
struct OptionalInt {
    int value;
    bool has_val;

    explicit operator bool() const { return has_val; }
    explicit operator int() const { return value; }
};

void variant4_conversion_operator() {
    OptionalInt oi{42, true};
    if (oi) {
        int v = static_cast<int>(oi);
        (void)v;
    }
}

// Variant 5: operator<< for stream output (bitcoin logging pattern)
struct Logger {
    int fd;
    Logger& operator<<(const char* msg) { (void)msg; return *this; }
    Logger& operator<<(int val) { (void)val; return *this; }
};

void variant5_stream_operator() {
    Logger log{1};
    log << "value: " << 42;
}

// Variant 6: operator() functor (bitcoin comparator pattern)
struct TxCompare {
    bool operator()(int a, int b) const { return a < b; }
};

void variant6_functor_operator() {
    TxCompare cmp;
    bool less = cmp(1, 2);
    (void)less;
}

// Variant 7: copy/move assignment operators with member data
struct Wallet {
    int* keys;
    size_t num_keys;

    Wallet& operator=(const Wallet& other) {
        if (this != &other) {
            keys = other.keys;
            num_keys = other.num_keys;
        }
        return *this;
    }
    Wallet& operator=(Wallet&& other) {
        keys = other.keys;
        num_keys = other.num_keys;
        other.keys = nullptr;
        other.num_keys = 0;
        return *this;
    }
};

void variant7_assignment_operators() {
    Wallet w1{nullptr, 0};
    Wallet w2{nullptr, 0};
    w2 = w1;
}

int main() {
    variant1_subscript_operator();
    variant2_comparison_operators();
    variant3_arithmetic_operators();
    variant4_conversion_operator();
    variant5_stream_operator();
    variant6_functor_operator();
    variant7_assignment_operators();
    return 0;
}
