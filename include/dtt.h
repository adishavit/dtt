#pragma once

// ============================================================
// DTT Library Header v2
// Supports: any property, compound constraints, arithmetic
// ============================================================

// --- Annotation macros ---
#define DTT_PROP(field, val)  [[clang::annotate("dtt:" #field "=" #val)]]
#define DTT_LEN(n)            [[clang::annotate("dtt:len=" #n)]]
#define DTT_SIZE(n)           [[clang::annotate("dtt:size=" #n)]]
#define DTT_CAPACITY(n)       [[clang::annotate("dtt:capacity=" #n)]]

#define DTT_REQUIRES(c)       [[clang::annotate("dtt:requires=" #c)]]
#define DTT_ENSURES(c)        [[clang::annotate("dtt:ensures=" #c)]]

// --- Generic Vec type ---
template<typename T>
struct Vec {
    T* data;
    int len;
    
    Vec() : data(nullptr), len(0) {}
    Vec(T* d, int l) : data(d), len(l) {}
};

// --- Buffer type with size and capacity ---
template<typename T>
struct Buffer {
    T* data;
    int size;
    int capacity;
    
    Buffer() : data(nullptr), size(0), capacity(0) {}
};

// --- Dependent functions ---

// head : requires len > 0
template<typename T>
DTT_REQUIRES(len > 0)
T head(Vec<T>& v) { return v.data[0]; }

// second : requires len > 1
template<typename T>
DTT_REQUIRES(len > 1)
T second(Vec<T>& v) { return v.data[1]; }

// last : requires len > 0
template<typename T>
DTT_REQUIRES(len > 0)
T last(Vec<T>& v) { return v.data[v.len - 1]; }

// pop : requires len > 0
template<typename T>
DTT_REQUIRES(len > 0)
T pop(Vec<T>& v) { return v.data[--v.len]; }

// Buffer operations
template<typename T>
DTT_REQUIRES(size > 0)
T bufferFirst(Buffer<T>& b) { return b.data[0]; }

template<typename T>
DTT_REQUIRES(size < capacity)
void bufferPush(Buffer<T>& b, T val) { b.data[b.size++] = val; }

// --- For std::vector ---
#include <vector>

template<typename T>
DTT_REQUIRES(len > 0)
T head(std::vector<T>& v) { return v[0]; }

template<typename T>
DTT_REQUIRES(len > 1)
T second(std::vector<T>& v) { return v[1]; }
