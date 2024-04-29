#include "frame.h"

/// @brief: this file exposes C API, used to test C++ against Rust implementation 

extern "C" Frame* new_frame(uint8_t sender, uint8_t receiver, uint8_t* cmd, size_t cmd_len) {
    Frame* frame = new Frame();

    frame->sender = sender;
    frame->receiver = receiver;

    //frame->data.resize(cmd_len);
    //std::memcpy(frame->data.data(), cmd, cmd_len);
    //std::copy(cmd, cmd + cmd_len, std::begin(frame->data));
    frame->data.push_slice(std::span(cmd, cmd_len));

    return frame;
}

extern "C" void free_frame(Frame* frame) {
    delete frame;
}

extern "C" void free_bytes(uint8_t* bytes) {
    delete[] bytes;
}

extern "C" SerializeError serialize_frame(const Frame& frame, uint8_t* &dst, size_t& len) {
    StaticVec<uint8_t, FRAME_MAX_SIZE * 2> v;
    SerializeError result = frame.serialize_into(v);

    dst = new uint8_t[v.size()];
    len = v.size();

    std::copy(std::begin(v.span()), std::end(v.span()), dst);

    return result;
}

extern "C" DeserializeError deserialize_frame(Frame& frame, uint8_t* data, size_t len) {
    return frame.deserialize_from(std::span(data, len));
}

extern "C" bool frame_eq(const Frame& f1, const Frame& f2) {
    return f1 == f2;
}

extern "C" void print_frame(const Frame& f) {
    printf("%d %d %zu\n", f.sender, f.receiver, f.data.size());

    for (size_t i = 0; i < f.data.size(); i++) {
        putchar(f.data.span()[i]);
    }

    printf("\n");
}
