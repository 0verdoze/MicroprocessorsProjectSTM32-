/*
 * usart_tx_handler.cpp
 *
 *  Created on: Nov 19, 2023
 *      Author: dacja
 */

#include "main.h"
#include "frame.h"
#include "command_handler.h"

#include <array>
#include <cstring>
#include <span>

#include <circular_buffer.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))

extern UART_HandleTypeDef huart2;

static CircularBuffer<FRAME_MAX_SIZE * 4> buffer;
static bool busy = false;

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart != &huart2) {
        // unreachable
        return;
    }

    // increment pointer
    size_t& tail = *buffer.getTail();
    tail = (tail + 1) % buffer.getCapacity();

    // check if we finished
    if (tail == *buffer.getHead()) {
        busy = false;
        return;
    }

    // transmit next byte
    HAL_UART_Transmit_IT(huart, buffer.getBuf() + tail, 1);
}

size_t UART_puts(std::span<const uint8_t> v);
void SendData(uint8_t receiver, bytes_view data) {
    Frame frame = {};

    frame.sender = LOCAL_ID;
    frame.receiver = receiver;
    frame.data.push_slice(data);

    StaticVec<uint8_t, FRAME_MAX_SIZE * 2> serialized;
    SerializeError err = frame.serialize_into(serialized);

    if (err != SerializeOk) {
        return;
    }

    size_t wrote = 0;
    while (wrote < serialized.size()) {
        wrote += UART_puts(serialized.span().last(serialized.size() - wrote));
    }
}

extern "C" void SendString(uint8_t receiver, const char* s) {
    SendData(receiver, bytes_view((uint8_t*)s, strlen(s)));
}

size_t UART_puts(std::span<const uint8_t> v) {
    uint8_t* data = buffer.getBuf();

    size_t& head = *buffer.getHead();
    size_t& tail = *buffer.getTail();
    size_t cap = buffer.getCapacity();

    __disable_irq();

    size_t idx = 0;
    while ((head + 1) % cap != tail && idx < v.size()) {
        data[head] = v[idx++];
        head = (head + 1) % cap;
    }

    if (head != tail && busy == false) {
        busy = true;
        HAL_UART_Transmit_IT(&huart2, data + tail, 1);
    }

    __enable_irq();

    return idx;
}
