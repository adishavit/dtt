// WHAT IS TESTED:
// Crash patterns from dragonfly codebase involving:
// - Complex template instantiations with STL containers
// - Hash maps and inline vectors (absl-like patterns)
// - Dependent type sizeof in template contexts
//
// Expected behavior: Should NOT crash (graceful handling)
//
// ============================================================================

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <new>

// -----------------------------------------------------------------------------
// VARIANT 1: Inline vector pattern (like absl::InlinedVector)
// -----------------------------------------------------------------------------
template<typename T, size_t N>
class InlinedVector {
    alignas(T) char inline_storage[sizeof(T) * N];
    T* data_ = reinterpret_cast<T*>(inline_storage);
    size_t size_ = 0;
    size_t capacity_ = N;

public:
    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }
    size_t size() const { return size_; }

    void push_back(const T& val) {
        if (size_ < capacity_) {
            new (&data_[size_++]) T(val);
        }
    }
};

// -----------------------------------------------------------------------------
// VARIANT 2: Fixed array pattern (like absl::FixedArray)
// -----------------------------------------------------------------------------
template<typename T, size_t N = 256 / sizeof(T)>
class FixedArray {
    T data_[N];
public:
    T& operator[](size_t i) { return data_[i]; }
    static constexpr size_t size() { return N; }
};

// -----------------------------------------------------------------------------
// VARIANT 3: Span-like view with dependent extent
// -----------------------------------------------------------------------------
template<typename T, size_t Extent = static_cast<size_t>(-1)>
class Span {
    T* data_;
    size_t size_;
public:
    Span(T* d, size_t s) : data_(d), size_(s) {}
    T& operator[](size_t i) { return data_[i]; }
    size_t size() const { return Extent == static_cast<size_t>(-1) ? size_ : Extent; }
};

// -----------------------------------------------------------------------------
// VARIANT 4: Hash node with complex type calculations
// -----------------------------------------------------------------------------
template<typename Key, typename Value>
struct HashNode {
    alignas(Key) char key_storage[sizeof(Key)];
    alignas(Value) char value_storage[sizeof(Value)];
    HashNode* next = nullptr;

    Key& key() { return *reinterpret_cast<Key*>(key_storage); }
    Value& value() { return *reinterpret_cast<Value*>(value_storage); }
};

template<typename Key, typename Value>
class HashMap {
    static constexpr size_t kNumBuckets = 16;
    HashNode<Key, Value>* buckets_[kNumBuckets] = {};

public:
    Value* find(const Key& k) {
        size_t idx = hash(k) % kNumBuckets;
        for (auto* node = buckets_[idx]; node; node = node->next) {
            if (node->key() == k) return &node->value();
        }
        return nullptr;
    }

private:
    size_t hash(const Key& k) { return static_cast<size_t>(k); }
};

// -----------------------------------------------------------------------------
// VARIANT 5: Buffer with runtime-dependent size calculation
// -----------------------------------------------------------------------------
template<typename T>
class DynamicBuffer {
    T* data_;
    size_t size_;
    size_t capacity_;

public:
    DynamicBuffer() : data_(nullptr), size_(0), capacity_(0) {}

    void reserve(size_t n) {
        if (n > capacity_) {
            T* new_data = static_cast<T*>(::operator new(n * sizeof(T)));
            if (data_) {
                for (size_t i = 0; i < size_; ++i) {
                    new (&new_data[i]) T(static_cast<T&&>(data_[i]));
                    data_[i].~T();
                }
                ::operator delete(data_);
            }
            data_ = new_data;
            capacity_ = n;
        }
    }

    T& operator[](size_t i) { return data_[i]; }
};

// -----------------------------------------------------------------------------
// VARIANT 6: String view with compile-time size
// -----------------------------------------------------------------------------
template<size_t N>
struct FixedString {
    char data_[N];
    static constexpr size_t size() { return N - 1; }
    char operator[](size_t i) const { return data_[i]; }
};

// -----------------------------------------------------------------------------
// VARIANT 7: Nested template with multiple dependent sizes
// -----------------------------------------------------------------------------
template<typename T, typename U>
struct Pair {
    alignas(T) char first_[sizeof(T)];
    alignas(U) char second_[sizeof(U)];

    T& first() { return *reinterpret_cast<T*>(first_); }
    U& second() { return *reinterpret_cast<U*>(second_); }
};

template<typename T, typename U, size_t N>
class PairArray {
    Pair<T, U> pairs_[N];
public:
    Pair<T, U>& operator[](size_t i) { return pairs_[i]; }
};

// Explicit instantiations
template class InlinedVector<int, 8>;
template class InlinedVector<double, 4>;
template class FixedArray<char>;
template class FixedArray<int, 10>;
template class Span<float>;
template class Span<int, 5>;
template class HashMap<int, int>;
template class DynamicBuffer<long>;
template class PairArray<int, double, 5>;

int main() {
    InlinedVector<int, 8> iv;
    iv.push_back(42);
    (void)iv[0];

    FixedArray<char> fa;
    (void)fa[0];

    int arr[10];
    Span<int> s(arr, 10);
    (void)s[0];

    HashMap<int, int> hm;
    (void)hm.find(1);

    DynamicBuffer<long> db;
    db.reserve(10);

    PairArray<int, double, 5> pa;
    (void)pa[0];

    return 0;
}
