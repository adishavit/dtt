// DTT-Tracked Vector — Wrapper around std::vector with compile-time tracking
#pragma once

#include <vector>
#include <cstddef>

namespace dtt {

template<typename T>
struct vector {
    std::vector<T> impl;
    
    // Constructors — set initial tracked size
    [[clang::annotate("dtt:ensures=_size == 0")]]
    vector() {}
    
    // TODO: initializer_list constructor would need size tracking
    
    // Size queries
    [[clang::annotate("dtt:returns=_size")]]
    size_t size() const { return impl.size(); }
    
    [[clang::annotate("dtt:ensures_if_true=_size == 0")]]
    [[clang::annotate("dtt:ensures_if_false=_size > 0")]]
    bool empty() const { return impl.empty(); }
    
    // Mutators
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push_back(const T& value) { impl.push_back(value); }
    
    [[clang::annotate("dtt:requires=_size > 0")]]
    [[clang::annotate("dtt:ensures=_size == old(_size) - 1")]]
    void pop_back() { impl.pop_back(); }
    
    [[clang::annotate("dtt:ensures=_size == 0")]]
    void clear() { impl.clear(); }
    
    // Element access
    [[clang::annotate("dtt:requires=_size > 0")]]
    T& front() { return impl.front(); }
    
    [[clang::annotate("dtt:requires=_size > 0")]]
    const T& front() const { return impl.front(); }
    
    [[clang::annotate("dtt:requires=_size > 0")]]
    T& back() { return impl.back(); }
    
    [[clang::annotate("dtt:requires=_size > 0")]]
    const T& back() const { return impl.back(); }
    
    // Unchecked access (use with caution)
    T& operator[](size_t i) { return impl[i]; }
    const T& operator[](size_t i) const { return impl[i]; }
    
    // Data access
    T* data() { return impl.data(); }
    const T* data() const { return impl.data(); }
    
    // Iterators
    auto begin() { return impl.begin(); }
    auto end() { return impl.end(); }
    auto begin() const { return impl.begin(); }
    auto end() const { return impl.end(); }
};

} // namespace dtt
