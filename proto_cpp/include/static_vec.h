#pragma once

#include <array>
#include <optional>

/** @brief Interface for StaticVec
 * 
 * Since StaticVec is static in size this allows us to
 * avoid generating multiple functions by the compiler that
 * would do pretty much the same thing
 */
template<typename T>
class IStaticVec {
public:
    virtual std::optional<T> push_back(T val) = 0;
    virtual std::optional<T> pop_back() = 0;

    virtual size_t size() const = 0;
    virtual size_t capacity() const = 0;
    virtual void clear() = 0;

    virtual std::span<T> span() = 0;
    virtual std::span<const T> span() const = 0;

    typedef std::span<T>::iterator iterator;
    typedef std::span<const T>::iterator const_iterator;

    virtual iterator begin() final { return std::begin(span()); }
    virtual const_iterator begin() const final { return std::begin(span()); }

    virtual iterator end() final { return std::end(span()); }
    virtual const_iterator end() const final { return std::end(span()); }

    virtual T* data() final { return span().data(); }
    virtual const T* data() const final { return span().data(); }

    virtual size_t push_slice(std::span<const T> data) final {
        size_t count = 0;

        for (auto& v : data) {
            if (push_back(v).has_value()) {
                break;
            }
        }

        return count;
    }
};

/** @brief Very simple Vec implentation with static capacity
 * 
 *  @note Use `span()` to access underlying data
 */
template<typename T, size_t SIZE>
class StaticVec final: public IStaticVec<T>{
public:
    StaticVec() = default;
    StaticVec(T init)
        : head { 1 }
        , inner { init }
    {}

    virtual std::optional<T> push_back(T val) final {
        if (head < SIZE) {
            inner[head++] = std::forward<T>(val);
            return std::nullopt;
        } else {
            return { val };
        }
    }

    virtual std::optional<T> pop_back() final {
        if (head > 0) {
            return { inner[--head] };
        } else {
            return std::nullopt;
        }
    }

    virtual void clear() final {
        head = 0;
    }

    virtual size_t size() const final {
        return head;
    }

    virtual size_t capacity() const final {
        return SIZE;
    }

    virtual std::span<T> span() final {
        return std::span(std::begin(inner), head);
    }

    virtual std::span<const T> span() const final {
        return std::span(std::begin(inner), head);
    }
private:
    size_t head = 0;
    std::array<T, SIZE> inner;
};
