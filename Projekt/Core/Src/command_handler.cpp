/*
 * command_handler.cpp
 *
 *  Created on: Nov 19, 2023
 *      Author: dacja
 */

#include "command_handler.h"
#include "error_codes.h"
#include "main.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include <ranges>

/// helper macro to print error code
#define WRITE_EC(ret, ec) (std::copy(std::begin(ec), std::end(ec), std::back_inserter(ret)))

/// @brief List of supported commands, with amount of parameters that they take
static constexpr std::array<CommandHandler, 5> kCommandHandlers = { {
    { "ON", CmdPwmOn, 0, 0 },
    { "OFF", CmdPwmOff, 0, 0 },
    { "SET_FREQ", CmdSetFreq, 1, 1 },
    { "SET_DUTY_CYCLES", CmdSetDutyCycles, 1, 312 },
    { "STATUS", CmdStatus, 0, 0 },
} };

std::optional<Frame> GetFrame() {
    // temporary disable interrupts, this will be restored when returning from this function
    __disable_irq();

    uint8_t* buf = gRxBuffer.getBuf();
    size_t& head = *(size_t*)gRxBuffer.getHead();
    size_t& tail = *(size_t*)gRxBuffer.getTail();
    size_t cap = gRxBuffer.getCapacity();

    // increase tail until it will point on `BEGIN_FRAME_BYTE`
    while (buf[tail] != BEGIN_FRAME_BYTE && tail != head) {
        tail = (tail + 1) % cap;
    }

    // early return if buffer is empty
    if (tail == head) {
        __enable_irq();
        return std::nullopt;
    }

    // if buffer is not empty it must point onto `BEGIN_FRAME_BYTE`
    // check if buffer contains `END_FRAME_BYTE`
    size_t idx = (tail + 1) % cap;
    while (idx != head && buf[idx] != END_FRAME_BYTE) {
        // check if we found another `BEGIN_FRAME_BYTE`
        // if we did, return from this function
        // alternativly we could just call ourself recursivly 
        if (buf[idx] == BEGIN_FRAME_BYTE) {
            tail = idx;
            __enable_irq();

            return std::nullopt;
        }

        idx = (idx + 1) % cap;
    }

    // if we reached end of a buffer, that means there is no `END_FRAME_BYTE`
    // return early
    if (head == idx || buf[idx] != END_FRAME_BYTE) {
        __enable_irq();
        return std::nullopt;
    }

    // buffer potentially contains a new frame, deserialize it
    Frame frame;
    auto err = frame.deserialize_from(gRxBuffer);
    // update beggining of the buffer, no mather if deserialization was a success 
    tail = (idx + 1) % cap;

    // reenable interrupts
    __enable_irq();

    // return new frame if successfull
    if (err == DeserializeOk) {
        return { frame };
    } else {
        return std::nullopt;
    }
}

/// @brief Split provided array by delimiter, and call `callback` on each chunk
/// @tparam T underlying data type
/// @param s array to be splitted by the delimiter
/// @param delim delimiter
/// @param callback function that will be called on each match (delimiter is not included)
/// @note `std::views::split` is not implemented for most C++20 implementations
template<typename T>
void Split(std::span<T> s, T delim, std::function<void(std::span<T>)> callback) {
    auto begin = std::begin(s);
    auto end = std::end(s);

    while (true) {
        auto found = std::find(begin, end, delim);

        callback(std::span(begin, found));
        if (found == end) {
            return;
        }

        begin = ++found;
    }
}

/// @brief Try to parse command, and its parameters from array of bytes
/// @param data array of bytes
/// @return Command and its arguments, or `std::nullpt` if `data` is empty
std::optional<ParsedCommand> ParseCommands(bytes_view data) {
    ParsedCommand parsed;
    Split<std::uint8_t>(data, static_cast<std::uint8_t>(' '), [&parsed](bytes_view arg) {
        if (std::size(arg) > 0) {
            parsed.args.push_back(arg);
        }
    });

    if (parsed.args.size() > 0) {
        return { parsed };
    } else {
        return std::nullopt;
    }
}

/// @brief Execute passed command
/// @param cmd Command and its arguments
/// @param ret Buffer that will receive `handler` response, or error if it occours
void ExecuteCommand(ParsedCommand& cmd, std::vector<uint8_t>& ret) {
    auto& args = cmd.args;

    if (args.size() == 0) {
        WRITE_EC(ret, kUnknownCommand);
        return;
    }

    // find correct handler
    auto found = std::find_if(std::begin(kCommandHandlers), std::end(kCommandHandlers), [&args](auto& handler) {
        return handler.commandName == std::string_view((char*)args[0].data(), args[0].size());
    });

    if (found == std::end(kCommandHandlers)) {
        WRITE_EC(ret, kUnknownCommand);
        return;
    }

    if (!(found->minArgs <= args.size() - 1 && found->maxArgs >= args.size() - 1)) {
        WRITE_EC(ret, kInvalidArgument);
        return;
    }

    found->callback(ret, args);
}

/// @brief Parses, and executes commands stored in RxBuffer
extern "C" void HandlePendingCommands(void) {
    while (true) {
        auto optional = GetFrame();

        if (!optional.has_value()) {
            return;
        }

        Frame& frame = *optional;
        if (frame.receiver != LOCAL_ID) {
            continue;
        }

        // extract commands
        auto command = ParseCommands(frame.data.span());

        if (command.has_value()) {
            bytes ret;
            // run callback for command
            ExecuteCommand(*command, ret);

            // send response back
            SendData(frame.sender, bytes_view(ret));
        }
    }
}

