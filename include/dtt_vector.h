// DTT-Tracked Vector — Drop-in std::vector replacement with compile-time safety
// Inherits from std::vector, overrides methods that need tracking
#pragma once

#include <vector>
#include <initializer_list>
#include <cstddef>

namespace dtt {

template<typename T, typename Allocator = std::allocator<T>>
class vector : public std::vector<T, Allocator> {
    using Base = std::vector<T, Allocator>;

public:
    // Type aliases (inherited, but explicit for clarity)
    using value_type = typename Base::value_type;
    using allocator_type = typename Base::allocator_type;
    using size_type = typename Base::size_type;
    using difference_type = typename Base::difference_type;
    using reference = typename Base::reference;
    using const_reference = typename Base::const_reference;
    using pointer = typename Base::pointer;
    using const_pointer = typename Base::const_pointer;
    using iterator = typename Base::iterator;
    using const_iterator = typename Base::const_iterator;
    using reverse_iterator = typename Base::reverse_iterator;
    using const_reverse_iterator = typename Base::const_reverse_iterator;

    // ========================================================================
    // CONSTRUCTORS
    // ========================================================================

    [[clang::annotate("dtt:ensures=_size == 0")]]
    vector() : Base() {}

    [[clang::annotate("dtt:ensures=_size == 0")]]
    explicit vector(const Allocator& alloc) : Base(alloc) {}

    // Count constructor - size becomes unknown (runtime value)
    explicit vector(size_type count, const Allocator& alloc = Allocator())
        : Base(count, alloc) {}

    vector(size_type count, const T& value, const Allocator& alloc = Allocator())
        : Base(count, value, alloc) {}

    template<class InputIt>
    vector(InputIt first, InputIt last, const Allocator& alloc = Allocator())
        : Base(first, last, alloc) {}

    vector(const vector& other) : Base(other) {}

    vector(const vector& other, const Allocator& alloc) : Base(other, alloc) {}

    vector(vector&& other) noexcept : Base(std::move(other)) {}

    vector(vector&& other, const Allocator& alloc) : Base(std::move(other), alloc) {}

    vector(std::initializer_list<T> init, const Allocator& alloc = Allocator())
        : Base(init, alloc) {}

    // ========================================================================
    // SIZE QUERIES
    // ========================================================================

    [[clang::annotate("dtt:returns=_size")]]
    size_type size() const noexcept { return Base::size(); }

    [[clang::annotate("dtt:ensures_if_true=_size == 0")]]
    [[clang::annotate("dtt:ensures_if_false=_size > 0")]]
    bool empty() const noexcept { return Base::empty(); }

    // max_size, capacity, reserve, shrink_to_fit - no tracking needed
    using Base::max_size;
    using Base::capacity;
    using Base::reserve;
    using Base::shrink_to_fit;

    // ========================================================================
    // ELEMENT ACCESS - Require _size > 0
    // ========================================================================

    [[clang::annotate("dtt:requires=_size > 0")]]
    reference front() { return Base::front(); }

    [[clang::annotate("dtt:requires=_size > 0")]]
    const_reference front() const { return Base::front(); }

    [[clang::annotate("dtt:requires=_size > 0")]]
    reference back() { return Base::back(); }

    [[clang::annotate("dtt:requires=_size > 0")]]
    const_reference back() const { return Base::back(); }

    // operator[] - unchecked access, no compile-time bounds check possible
    using Base::operator[];

    // at() - has runtime bounds check, no compile-time annotation needed
    using Base::at;

    // data() - no annotation needed
    using Base::data;

    // ========================================================================
    // MODIFIERS - Track size changes
    // ========================================================================

    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push_back(const T& value) { Base::push_back(value); }

    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push_back(T&& value) { Base::push_back(std::move(value)); }

    template<class... Args>
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    reference emplace_back(Args&&... args) {
        return Base::emplace_back(std::forward<Args>(args)...);
    }

    [[clang::annotate("dtt:requires=_size > 0")]]
    [[clang::annotate("dtt:ensures=_size == old(_size) - 1")]]
    void pop_back() { Base::pop_back(); }

    [[clang::annotate("dtt:ensures=_size == 0")]]
    void clear() noexcept { Base::clear(); }

    // resize - sets size to runtime value (can't track precisely)
    // But we CAN track resize(0) as _size == 0
    void resize(size_type count) { Base::resize(count); }
    void resize(size_type count, const value_type& value) { Base::resize(count, value); }

    // insert - increases size (complex return type, multiple overloads)
    // Size change depends on how many elements inserted
    using Base::insert;

    // emplace - increases size by 1
    template<class... Args>
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    iterator emplace(const_iterator pos, Args&&... args) {
        return Base::emplace(pos, std::forward<Args>(args)...);
    }

    // erase - decreases size (by 1 or range)
    using Base::erase;

    // swap - exchanges contents (and tracked sizes)
    using Base::swap;

    // assign - replaces contents
    using Base::assign;

    // ========================================================================
    // ITERATORS - No tracking needed, inherit all
    // ========================================================================
    using Base::begin;
    using Base::end;
    using Base::cbegin;
    using Base::cend;
    using Base::rbegin;
    using Base::rend;
    using Base::crbegin;
    using Base::crend;

    // ========================================================================
    // ALLOCATOR
    // ========================================================================
    using Base::get_allocator;
};

// Deduction guides (C++17)
template<class InputIt,
         class Alloc = std::allocator<typename std::iterator_traits<InputIt>::value_type>>
vector(InputIt, InputIt, Alloc = Alloc())
    -> vector<typename std::iterator_traits<InputIt>::value_type, Alloc>;

template<class T, class Alloc = std::allocator<T>>
vector(std::initializer_list<T>, Alloc = Alloc()) -> vector<T, Alloc>;

// ============================================================================
// NON-MEMBER FUNCTIONS
// ============================================================================

template<class T, class Alloc>
bool operator==(const vector<T, Alloc>& lhs, const vector<T, Alloc>& rhs) {
    return static_cast<const std::vector<T, Alloc>&>(lhs) ==
           static_cast<const std::vector<T, Alloc>&>(rhs);
}

template<class T, class Alloc>
bool operator!=(const vector<T, Alloc>& lhs, const vector<T, Alloc>& rhs) {
    return !(lhs == rhs);
}

template<class T, class Alloc>
bool operator<(const vector<T, Alloc>& lhs, const vector<T, Alloc>& rhs) {
    return static_cast<const std::vector<T, Alloc>&>(lhs) <
           static_cast<const std::vector<T, Alloc>&>(rhs);
}

template<class T, class Alloc>
bool operator<=(const vector<T, Alloc>& lhs, const vector<T, Alloc>& rhs) {
    return !(rhs < lhs);
}

template<class T, class Alloc>
bool operator>(const vector<T, Alloc>& lhs, const vector<T, Alloc>& rhs) {
    return rhs < lhs;
}

template<class T, class Alloc>
bool operator>=(const vector<T, Alloc>& lhs, const vector<T, Alloc>& rhs) {
    return !(lhs < rhs);
}

template<class T, class Alloc>
void swap(vector<T, Alloc>& lhs, vector<T, Alloc>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
}

} // namespace dtt
