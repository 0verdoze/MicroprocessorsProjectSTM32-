#pragma once

#include "frame.h"

#include <optional>
#include <queue>
#include <vector>
#include <string_view>
#include <functional>

#include <circular_buffer.h>
#include <static_vec.h>

/// id used to represent this device in `Frame`, sender and receiver fields
#define LOCAL_ID 100

using bytes = std::vector<std::uint8_t>;
using bytes_view = std::span<std::uint8_t>;

/// @brief Struct representing parsed command
/// @note args[0] is command name
struct ParsedCommand {
	std::vector<bytes_view> args;
};

/// @brief Struct representing command handler
/// @param commandName command name (args[0])
/// @param callback function to be called to handle this command
/// @param minArgs minimum amount of arguments to call `callback`
/// @param maxArgs maximum amount of arguments to call `callback` (inclusive)
struct CommandHandler {
	std::string_view commandName;
	void (*callback)(bytes&, std::span<bytes_view>);
	size_t minArgs;
	size_t maxArgs;
};

/// @brief Informations to determine what device is doing right now
struct DeviceState {
    std::vector<uint32_t> dutyCycles = { 0 };
    std::vector<uint8_t> userDutyCycles;
    volatile bool isPwmGenerated;
};

/// @brief `CircularBuffer` used to receive data
/// @note Buffer for transmission is defined in `usart_tx_handler.cpp` file
inline CircularBuffer<FRAME_MAX_SIZE * 4> gRxBuffer;
inline DeviceState gDeviceState;

/// @brief Send data over USART to host device
/// @param receiver ID of target device
/// @param data Data that are going to be send
/// @note This function will block until all data can be push into TxBuffer
void SendData(uint8_t receiver, bytes_view data);

// TODO: replace parameters, with a single struct as a argument
/// Macro to define callback function
#define DEF_CMD(name) void name(bytes& ret, std::span<bytes_view> args)

/// @brief enable PWM signal generation 
DEF_CMD(CmdPwmOn);

/// @brief disable PWM signal generation 
DEF_CMD(CmdPwmOff);

/// @brief set generated PWM signal frequency 
DEF_CMD(CmdSetFreq);

/// @brief set generated PWM duty cycles 
DEF_CMD(CmdSetDutyCycles);

/// @brief check status of PWM signal generation
DEF_CMD(CmdStatus);
