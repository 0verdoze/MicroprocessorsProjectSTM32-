#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <span>

#include "static_vec.h"
#include "circular_buffer.h"

/// @brief Escape byte that is used while encoding bytes
/// @note Refer to `ESCAPE_TABLE` for exact escape sequences
constexpr uint8_t ESCAPE_BYTE = 0x1B;

/// @brief Byte that must be at the beggining of each serialized `Frame`
constexpr uint8_t BEGIN_FRAME_BYTE = (uint8_t)'(';
/// @brief Byte that must be at the end of each serialized `Frame`
constexpr uint8_t END_FRAME_BYTE = (uint8_t)')';

/// @brief Lookup table for escape characters
/// @note Character on the left is replaced with sequence of { `ESCAPE_BYTE`, right byte }
static constexpr uint8_t ESCAPE_TABLE[3][2] = {
    { ESCAPE_BYTE, 0x41 },
    { BEGIN_FRAME_BYTE, 0x42 },
    { END_FRAME_BYTE, 0x43 },
};

/// @brief Maximum frame size (pre encoding) of any `Frame`
constexpr size_t FRAME_MAX_SIZE = 1280;

/// @brief Minimum frame size of any `Frame`
constexpr size_t FRAME_MIN_SIZE = 10;

/// @brief Maximum `Frame::data` size
constexpr size_t FRAME_DATA_MAX_SIZE = FRAME_MAX_SIZE - FRAME_MIN_SIZE - 2;

/// @brief Error that might be returned during deserialization of a `Frame`
enum DeserializeError {
    /// @brief Deserialization was successfull
    DeserializeOk,
    /// @brief Frame must start with `BEGIN_FRAME_BYTE`, however provided data did not
    InvalidStartByte,
    /// @brief Frame must end with `END_FRAME_BYTE`, however provided data did not
    InvalidEndByte,
    /// @brief Provided data is not enough to deserialize `Frame`
    UnexpectedEOF,
    /// @brief Internal deserializer error, this should never occour
    ExpectedEOF,
    /// @brief CRC32 checksum for deserialized `Frame` is different then what was expected
    /// @note `Frame` is fully deserialized in this scenario, but data might be corrupted
    CRC32MissMatch,
    /// @brief Provided data contains unknown escape sequence, there is a chance that some bytes
    /// @brief were dropped by the underlying connection
    InvalidEscapeSequence,
    /// @brief Provided data would result in a `Frame` larger than `FRAME_MAX_SIZE`
    DataTooBig,
    /// @brief An attempt was made to decode begin or end byte, that may indicate
    /// @brief that some bytes were dropped by the underlying connection
    InvalidByte,
};

/// @brief Error that might be returned during serialization of a `Frame`
enum SerializeError {
    /// @brief Serialization was successfull
    SerializeOk,
    /// @brief Serialized frame size (prior to encoding) is larger than `MAX_FRAME_SIZE`
    FrameTooLongError,
    /// @brief Provided buffer for serialization doesn't have enough capacity to serialize frame into it
    BufferTooSmall,
};

/// @brief Frame representing, sender, receiver and data that sender can/has send
/// @note All serialized bytes are in Big Endian byte order
/// @note Representation in wire format
/// @note [ SENDER  RECEIVER  DATA_LEN  DATA  CRC32 ]
/// 
/// @note `[` - `BEGIN_FRAME_BYTE` 1 byte
/// @note * `SENDER` - sender id, 1 byte
/// @note * `RECEIVER` - receiver id, 1 byte
/// @note * `DATA_LEN` - `DATA` field length, 2 bytes
/// @note * `DATA` - sequence of bytes, `DATA_LEN` bytes
/// @note * `CRC32` - CRC32 checksum of `SENDER`, `RECEIVER`, `DATA_LEN`, `DATA` fields, 4 bytes
/// @note `]` - `END_FRAME_BYTE` 1 byte
struct Frame {
    /// @brief sender of this frame
    std::uint8_t sender;

    /// @brief receiver of this frame
    std::uint8_t receiver;

    /// @brief data contained in this frame
    StaticVec<uint8_t, FRAME_DATA_MAX_SIZE> data;

    /// @brief Serializes frame into provided container
    /// @return `SerializeOk` if successfull, according error otherwise
    SerializeError serialize_into(IStaticVec<std::uint8_t>&) const;

    /// @brief Deserializes `Frame` from provided data
    /// @param encoded_data data to deserialize `Frame` from
    /// @return `DeserializeOk` if successfull, according error otherwise
    DeserializeError deserialize_from(std::span<const std::uint8_t> encoded_data);

    /// @brief Deserializes `Frame` from provided data
    /// @param encoded_data data to deserialize `Frame` from
    /// @return `DeserializeOk` if successfull, according error otherwise
    /// @note caller must guarantee that `circular_buffer` constains `END_FRAME_BYTE`
    DeserializeError deserialize_from(ICircularBuffer& circular_buffer);

    /// @brief Calculates CRC32 checksum of this frame
    /// @return CRC32 checksum
    std::uint32_t crc32() const;

    friend bool operator==(const Frame& lhs, const Frame& rhs);

private:
    DeserializeError deserialize_from_decoded(IStaticVec<std::uint8_t>&);
};
