#pragma once

#include <cstdint>
#include <optional>

/** @brief Interface for CircularBuffer
 * 
 * @note Since CircularBuffer is static in size this allows us to
 * avoid generating multiple functions by the compiler that
 * would do pretty much the same thing
 */
class ICircularBuffer {
public:
    virtual std::optional<uint8_t> push_head(uint8_t val) = 0;
    virtual std::optional<uint8_t> pop_tail() = 0;
    virtual uint8_t* getNextAddr() = 0;
    virtual void advance_head() = 0;

    virtual uint8_t* getBuf() = 0;
    virtual size_t* getHead() = 0;
    virtual size_t* getTail() = 0;
    virtual size_t getSize() = 0;
    virtual size_t getCapacity() = 0;
};

/// @brief Implementation of CircularBuffer
template<size_t SIZE>
class CircularBuffer: public ICircularBuffer {
public:
    virtual std::optional<uint8_t> push_head(uint8_t val) final {
        auto incr_head = (head + 1) % SIZE;
        if (incr_head != tail) {
            buf[head] = val;
            head = incr_head;

            return std::nullopt;
        } else {
            return { val };
        }
    }

    virtual std::optional<uint8_t> pop_tail() final {
        if (tail != head) {
            uint8_t byte = buf[tail];
            tail = (tail + 1) % SIZE;

            return { byte };
        } else {
            return std::nullopt;
        }
    }

    virtual uint8_t* getNextAddr() final {
        return buf + head;
    }

    virtual void advance_head() final {
        head = (head + 1) % SIZE;

        if (head == tail) {
            tail = (tail + 1) % SIZE;
        }
    }

    virtual uint8_t* getBuf() final {
        return buf;
    }

    virtual size_t* getHead() final {
        return &head;
    }

    virtual size_t* getTail() final {
        return &tail;
    }

    virtual size_t getSize() final {
        if (head > tail) {
            return head - tail;
        } else {
            return SIZE - tail + head;
        }
    }

    virtual size_t getCapacity() final {
        return SIZE;
    }

private:
    size_t tail{}, head{};
    uint8_t buf[SIZE];
};
