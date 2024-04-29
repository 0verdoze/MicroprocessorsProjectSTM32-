#include "frame.h"
#include "bytes.h"
#include "static_vec.h"

#include <array>
#include <cstdint>
#include <optional>


/// @brief Encoded byte using `ESCAPE_TABLE`
/// @param byte byte to be encoded
/// @param out container to store the result
void encode_byte(uint8_t byte, IStaticVec<uint8_t>& out) {
    for (auto e : ESCAPE_TABLE) {
        // check if we found a match
        if (byte == e[0]) {
            // push the sequence and return early
            out.push_back(ESCAPE_BYTE);
            out.push_back(e[1]);
            return;
        }
    }

    // no need to escape
    out.push_back(byte);
}

/// @brief Helper function to escape multiple bytes at once
/// @param data bytes to be escaped using `encode_byte`
/// @param out container to store the result
void encode_bytes(std::span<const uint8_t> data, IStaticVec<uint8_t>& out) {
    for (auto b : data) {
        encode_byte(b, out);
    }
}

/// @brief Helper function to serialize (and escape) passed frame
/// @note This function does not include checksum, `BEGIN_FRAME_BYTE` nor `END_FRAME_BYTE` in `out`
/// @param frame frame to be serialized
/// @param out container to store the result
/// @return `SerializeOk` if successfull, appropriate error otherwise
SerializeError serialize_fields(const Frame& frame, IStaticVec<uint8_t>& out) {
    // macro that stores field as sequence of be bytes, and then encodes them (and store into `out`)
    #define serialize_field(field) {    \
            auto var = field;   \
                \
            auto arr = to_be_bytes(var);    \
            encode_bytes(std::span(arr), out);  \
        }

    // serialize all fields up to data
    serialize_field(frame.sender)
    serialize_field(frame.receiver)
    serialize_field((std::uint16_t)frame.data.size())
    
    // serialize `data` field
    encode_bytes(frame.data.span(), out);

    return SerializeOk;
    #undef serialize_field
}

SerializeError Frame::serialize_into(IStaticVec<uint8_t>& out) const {
    // if data.size() > FRAME_DATA_MAX_SIZE, than frame size will be > FRAME_MAX_SIZE
    // which cannot happen
    if (data.size() > FRAME_DATA_MAX_SIZE) {
        return FrameTooLongError;
    }

    // frame must begin with `BEGIN_FRAME_BYTE`
    out.push_back(BEGIN_FRAME_BYTE);

    // serialize sender, receiver, data_len and data fields
    SerializeError err = serialize_fields(*this, out);
    if (err != SerializeOk) {   
        return err;
    }

    // calculate, encode and write checksum
    std::array<uint8_t, 4> crc = to_be_bytes(crc32());
    encode_bytes(crc, out);
    
    // each frame must end with `END_FRAME_BYTE`
    bool buffer_full = out.push_back(END_FRAME_BYTE).has_value();

    // check if buffer wasn't too small
    if (buffer_full) {
        return BufferTooSmall;
    }

    return SerializeOk;
}

/// @brief Decode single byte
/// @param data sequence of encoded bytes
/// @param out location to store decoded byte
/// @param read location to store amount of read bytes from `data`
/// @return `DeserializeOk` if successfull, appropriate error otherwise
DeserializeError decode_byte(std::span<const uint8_t> data, uint8_t& out, size_t& read) {
    // in case of early return set read to 0
    read = 0;

    // data doesn't contain any data, return error
    if (data.size() == 0) {
        return UnexpectedEOF;
    }

    if (data[0] == ESCAPE_BYTE) {
        // ESCAPE_BYTE must be followed by another byte
        if (data.size() == 1) {
            return UnexpectedEOF;
        }

        for (auto e : ESCAPE_TABLE) {
            if (e[1] == data[1]) {
                // we found byte in table, return it
                out = e[0];
                read = 2;
                return DeserializeOk;
            }
        }

        return InvalidEscapeSequence;
    } else if (data[0] == BEGIN_FRAME_BYTE || data[0] == END_FRAME_BYTE) {
        // `BEGIN_FRAME_BYTE` and `END_FRAME_BYTE` must be always escaped
        // this may indicate that some data was dropped be the underlying connection
        return InvalidByte;
    } else {
        // we don't need to decode this byte, return it 
        out = data[0];
        read = 1;
        return DeserializeOk;
    }
}

/// @brief decode multiple bytes
/// @param data sequence of encoded bytes
/// @param out location to store decoded bytes
/// @return `DeserializeOk` if successfull, appropriate error otherwise
DeserializeError decode_bytes(std::span<const uint8_t> data, IStaticVec<uint8_t>& out) {
    size_t read;
    uint8_t decoded;

    while (data.size()) {
        DeserializeError err = decode_byte(data, decoded, read);

        if (err != DeserializeOk) {
            return err;
        }

        out.push_back(decoded);
        // advance `data` by `read` bytes
        data = data.last(data.size() - read);
    }

    return DeserializeOk;
}

/// @brief deserialize type from bytes
/// @tparam T any type that can be used with `from_be_bytes` function
/// @param data decoded sequence of bytes to serialize `T` from
/// @param idx idx + amount of bytes read from `data`
/// @param result 
/// @return `DeserializeOk` if successfull, appropriate error otherwise
template<typename T>
DeserializeError deserialize(std::span<const uint8_t> data, size_t& idx, T& result) {
    if (idx + sizeof(T) > data.size()) {
        return UnexpectedEOF;
    }

    // copy required amount of bytes to array
    std::array<uint8_t, sizeof(T)> bytes = {};
    std::copy(std::begin(data) + idx, std::begin(data) + idx + sizeof(T), std::begin(bytes));
    idx += sizeof(T);

    result = from_be_bytes<T>(bytes);

    return DeserializeOk;
}

DeserializeError Frame::deserialize_from(std::span<const uint8_t> encoded) {
    if (encoded.size() < FRAME_MIN_SIZE) {
        return UnexpectedEOF;
    }

    if (encoded[0] != BEGIN_FRAME_BYTE) {
        return InvalidStartByte;
    }

    if (encoded[encoded.size() - 1] != END_FRAME_BYTE) {
        return InvalidEndByte;
    }

    StaticVec<uint8_t, FRAME_MAX_SIZE> decoded;
    auto err = decode_bytes(std::span(std::begin(encoded) + 1, std::end(encoded) - 1), decoded);

    if (err != DeserializeOk) {
        return err;
    }

    return deserialize_from_decoded(decoded);
}

/// SAFETY: caller must guarantee that `CircularBuffer` constains `END_FRAME_BYTE`
DeserializeError Frame::deserialize_from(ICircularBuffer& circular_buffer) {
    uint8_t* buf = circular_buffer.getBuf();

    // circular_buffer must begin with `BEGIN_FRAME_BYTE`
    if (buf[*circular_buffer.getTail()] != BEGIN_FRAME_BYTE) {
        return InvalidStartByte;
    }

    // skip if its impossible to deserialize from too little data 
    if (circular_buffer.getSize() < FRAME_MIN_SIZE) {
        return UnexpectedEOF;
    }

    // `FRAME_MAX_SIZE` include `BEGIN_FRAME_BYTE`, and `END_FRAME_BYTE`
    // so we `FRAME_MAX_SIZE - 2` to account for that 
    StaticVec<uint8_t, FRAME_MAX_SIZE - 2> decoded;
    size_t tail = *circular_buffer.getTail();

    // head should point at last element in the buffer
    // if idx == head, then buf[head] must be equal to END_FRAME_BYTE
    size_t head = (*circular_buffer.getHead() ? *circular_buffer.getHead() : circular_buffer.getCapacity()) - 1;
    size_t cap = circular_buffer.getCapacity();

    // we can skip first byte as we know that its `BEGIN_FRAME_BYTE`
    size_t idx = (tail + 1) % cap;
    size_t read;
    uint8_t window[2];
    uint8_t b;

    // there is no risk of buffer overrun
    // as caller must gurantee that this buffer contains `END_FRAME_BYTE`
    // if there ever will be { ESCAPE_BYTE, END_FRAME_BYTE }, decode_byte will return an error
    while (idx != head) {
        // read bytes in pairs, so we can decode them
        window[0] = buf[idx];
        window[1] = buf[(idx + 1) % cap];

        if (buf[idx] == END_FRAME_BYTE) {
            break;
        }

        auto err = decode_byte(std::span(window, 2), b, read);
        if (err != DeserializeOk) {
            return err;
        }

        if (decoded.push_back(b).has_value()) {
            return DataTooBig;
        }

        idx = (idx + read) % cap;
    }

    if (buf[idx] == END_FRAME_BYTE) {
        // since data decoded successfully, we can deserialize it
        return deserialize_from_decoded(decoded);
    } else {
        return UnexpectedEOF;
    }
}

DeserializeError Frame::deserialize_from_decoded(IStaticVec<std::uint8_t>& decoded) {
    #define deserialize_field(field) {    \
            DeserializeError err = deserialize(decoded.span(), idx, field);   \
            if (err != DeserializeOk) {   \
                return err; \
            }   \
        }

    // ammount of data we used, deserialize() will add size of each field to it
    size_t idx = 0;

    deserialize_field(sender);
    deserialize_field(receiver);
    
    uint16_t data_len;
    deserialize_field(data_len);

    if (data_len > FRAME_DATA_MAX_SIZE) {
        return DataTooBig;
    }

    // check if decoded has enough bytes to decode data field
    if (idx + data_len >= decoded.size()) {
        return UnexpectedEOF;
    }

    // data may already contain some data, clear it
    data.clear();
    data.push_slice(std::span(std::begin(decoded) + idx, (size_t)data_len));
    idx += data_len;

    // deserialize crc, and compare checksums
    uint32_t crc;
    deserialize_field(crc);

    if (idx != decoded.size()) {
        return ExpectedEOF;
    }

    if (crc != crc32()) {
        return CRC32MissMatch;
    }

    return DeserializeOk;
    #undef deserialize_field
}

uint32_t crc32_calculate(uint8_t* const data, size_t len) {
    size_t i, j;
    unsigned int crc, msb;

    crc = 0xFFFFFFFF;
    for(i = 0; i < len; i++) {
        // xor next byte to upper bits of crc
        crc ^= ((unsigned int)data[i]) << 24;
        for (j = 0; j < 8; j++) {    // Do eight times.
            msb = crc >> 31;
            crc <<= 1;
            crc ^= (0 - msb) & 0x04C11DB7;
        }
    }

    return crc; // don't complement crc on output
}

std::uint32_t Frame::crc32() const {
    #define serialize_field(field) {    \
            auto var = field;   \
                \
            auto arr = to_be_bytes(var);    \
            buf.push_slice(std::span(arr));  \
        }
    
    // we put all data as deserialize_into would, but without encoding
    // TODO: instead of using separate buffer, pass data directly into crc32
    StaticVec<uint8_t, FRAME_MAX_SIZE> buf;

    serialize_field(sender);
    serialize_field(receiver);
    serialize_field((std::uint16_t)data.size());
    buf.push_slice(data.span());

    // pad with zeroes
    while (buf.size() % 4 != 0) {
        buf.push_back(0);
    }

    return crc32_calculate(buf.span().data(), buf.size());
    #undef serialize_field
}

bool operator==(const Frame& lhs, const Frame& rhs) {
    auto lhs_data = lhs.data.span(), rhs_data = rhs.data.span();

    return lhs.sender == rhs.sender
        && lhs.receiver == rhs.receiver
        && lhs_data.size() == rhs_data.size()
        && std::equal(std::begin(lhs_data), std::end(lhs_data), std::begin(rhs_data), std::end(rhs_data));
}

