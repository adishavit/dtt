#pragma once

// ============================================================
// DTT Library Header
// Include this to get dependent-typed vector operations
// ============================================================

// --- Annotation macros for convenience ---
#define DTT_LEN(n)        [[clang::annotate("dtt:len=" #n)]]
#define DTT_REQUIRES(c)   [[clang::annotate("dtt:requires=" #c)]]

// --- Generic Vec type (works with any T) ---
template<typename T>
struct Vec {
    T* data;
    int len;
    
    Vec() : data(nullptr), len(0) {}
    Vec(T* d, int l) : data(d), len(l) {}
    
    T& operator[](int i) { return data[i]; }
    const T& operator[](int i) const { return data[i]; }
};

// --- Dependent functions (preconditions in annotations) ---

// head : Vec T → T  (requires len > 0)
template<typename T>
DTT_REQUIRES(len > 0)
T head(Vec<T>& v) { 
    return v.data[0]; 
}

// second : Vec T → T  (requires len > 1)
template<typename T>
DTT_REQUIRES(len > 1)
T second(Vec<T>& v) { 
    return v.data[1]; 
}

// last : Vec T → T  (requires len > 0)
template<typename T>
DTT_REQUIRES(len > 0)
T last(Vec<T>& v) { 
    return v.data[v.len - 1]; 
}

// --- For std::vector users ---
#include <vector>

template<typename T>
DTT_REQUIRES(len > 0)
T head(std::vector<T>& v) { 
    return v[0]; 
}

template<typename T>
DTT_REQUIRES(len > 1)
T second(std::vector<T>& v) { 
    return v[1]; 
}
