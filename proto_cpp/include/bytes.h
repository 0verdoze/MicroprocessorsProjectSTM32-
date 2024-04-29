#pragma once

#include <array>
#include <bit>
#include <cstdint>


// helper functions to cast numbers to and from bytes

template<typename T, typename = std::enable_if<std::is_trivial_v<T> && std::is_unsigned_v<T>>>
std::array<std::uint8_t, sizeof(T)> to_be_bytes(const T& n) {
    auto result = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(n);

    if constexpr (std::endian::native == std::endian::little) {
        std::reverse(std::begin(result), std::end(result));
    } else if constexpr (std::endian::native == std::endian::big) {
        // no need to do anything, result is already in big endian order
    } else {
        static_assert(std::endian::native == std::endian::big
        || std::endian::native == std::endian::little, "unsupported target");
    }

    return result;
}

template<typename T, typename = std::enable_if<std::is_trivial_v<T> && std::is_unsigned_v<T>>>
std::array<std::uint8_t, sizeof(T)> to_le_bytes(const T& n) {
    auto result = std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(n);

    if constexpr (std::endian::native == std::endian::little) {
        // no need to do anything, result is already in little endian order
    } else if constexpr (std::endian::native == std::endian::big) {
        std::reverse(std::begin(result), std::end(result));
    } else {
        static_assert(std::endian::native == std::endian::big
        || std::endian::native == std::endian::little, "unsupported target");
    }

    return result;
}

template<typename T, typename = std::enable_if<std::is_trivial_v<T> && std::is_unsigned_v<T>>>
T from_be_bytes(std::array<std::uint8_t, sizeof(T)> arr) {
    if constexpr (std::endian::native == std::endian::little) {
        std::reverse(std::begin(arr), std::end(arr));
    } else if constexpr (std::endian::native == std::endian::big) {
        // no need to do anything, result is already in native endian order
    } else {
        static_assert(std::endian::native == std::endian::big
        || std::endian::native == std::endian::little, "unsupported target");
    }

    return std::bit_cast<T>(arr);
}

template<typename T, typename = std::enable_if<std::is_trivial_v<T> && std::is_unsigned_v<T>>>
T from_le_bytes(std::array<std::uint8_t, sizeof(T)> arr) {
    if constexpr (std::endian::native == std::endian::little) {
        // no need to do anything, result is already in native endian order
    } else if constexpr (std::endian::native == std::endian::big) {
        std::reverse(std::begin(arr), std::end(arr));
    } else {
        static_assert(std::endian::native == std::endian::big
        || std::endian::native == std::endian::little, "unsupported target");
    }

    return std::bit_cast<T>(arr);
}
