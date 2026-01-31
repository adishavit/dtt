// dtt_expected.h - Minimal std::expected polyfill for DTT testing
// This mimics the C++23 std::expected API for testing purposes

#ifndef DTT_EXPECTED_H
#define DTT_EXPECTED_H

#include <utility>
#include <type_traits>
#include <new>

namespace std {

// std::unexpected - wraps an error value
template<typename E>
class unexpected {
    E error_;
public:
    constexpr explicit unexpected(const E& e) : error_(e) {}
    constexpr explicit unexpected(E&& e) : error_(std::move(e)) {}
    constexpr const E& error() const& { return error_; }
    constexpr E& error() & { return error_; }
    constexpr E&& error() && { return std::move(error_); }
};

template<typename E>
unexpected(E) -> unexpected<E>;

// std::expected<T, E> - holds either a value or an error
template<typename T, typename E>
class expected {
    union {
        T value_;
        E error_;
    };
    bool has_value_;

public:
    using value_type = T;
    using error_type = E;
    using unexpected_type = unexpected<E>;

    // Default constructor - value-initializes T
    constexpr expected() : value_(), has_value_(true) {}

    // Value constructors
    constexpr expected(const T& v) : value_(v), has_value_(true) {}
    constexpr expected(T&& v) : value_(std::move(v)), has_value_(true) {}

    // Error constructor from unexpected
    constexpr expected(const unexpected<E>& u) : error_(u.error()), has_value_(false) {}
    constexpr expected(unexpected<E>&& u) : error_(std::move(u).error()), has_value_(false) {}

    // Copy/move constructors
    constexpr expected(const expected& other) : has_value_(other.has_value_) {
        if (has_value_) new (&value_) T(other.value_);
        else new (&error_) E(other.error_);
    }
    constexpr expected(expected&& other) : has_value_(other.has_value_) {
        if (has_value_) new (&value_) T(std::move(other.value_));
        else new (&error_) E(std::move(other.error_));
    }

    // Destructor
    ~expected() {
        if (has_value_) value_.~T();
        else error_.~E();
    }

    // Assignment from value
    expected& operator=(const T& v) {
        if (has_value_) value_ = v;
        else {
            error_.~E();
            new (&value_) T(v);
            has_value_ = true;
        }
        return *this;
    }
    expected& operator=(T&& v) {
        if (has_value_) value_ = std::move(v);
        else {
            error_.~E();
            new (&value_) T(std::move(v));
            has_value_ = true;
        }
        return *this;
    }

    // Assignment from unexpected
    expected& operator=(const unexpected<E>& u) {
        if (!has_value_) error_ = u.error();
        else {
            value_.~T();
            new (&error_) E(u.error());
            has_value_ = false;
        }
        return *this;
    }
    expected& operator=(unexpected<E>&& u) {
        if (!has_value_) error_ = std::move(u).error();
        else {
            value_.~T();
            new (&error_) E(std::move(u).error());
            has_value_ = false;
        }
        return *this;
    }

    // Observers
    constexpr bool has_value() const noexcept { return has_value_; }
    constexpr explicit operator bool() const noexcept { return has_value_; }

    // Value access - UB if no value
    constexpr T& operator*() & { return value_; }
    constexpr const T& operator*() const& { return value_; }
    constexpr T&& operator*() && { return std::move(value_); }

    constexpr T* operator->() { return &value_; }
    constexpr const T* operator->() const { return &value_; }

    // value() - throws if no value (we just return for DTT purposes)
    constexpr T& value() & { return value_; }
    constexpr const T& value() const& { return value_; }
    constexpr T&& value() && { return std::move(value_); }

    // error() - UB if has value
    constexpr E& error() & { return error_; }
    constexpr const E& error() const& { return error_; }
    constexpr E&& error() && { return std::move(error_); }

    // value_or - always safe
    template<typename U>
    constexpr T value_or(U&& default_value) const& {
        return has_value_ ? value_ : static_cast<T>(std::forward<U>(default_value));
    }
    template<typename U>
    constexpr T value_or(U&& default_value) && {
        return has_value_ ? std::move(value_) : static_cast<T>(std::forward<U>(default_value));
    }

    // emplace - construct value in-place
    template<typename... Args>
    T& emplace(Args&&... args) {
        if (!has_value_) {
            error_.~E();
            has_value_ = true;
        } else {
            value_.~T();
        }
        new (&value_) T(std::forward<Args>(args)...);
        return value_;
    }
};

} // namespace std

#endif // DTT_EXPECTED_H
