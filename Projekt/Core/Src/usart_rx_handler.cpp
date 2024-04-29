#include "command_handler.h"
#include "main.h"

#include <circular_buffer.h>

extern UART_HandleTypeDef huart2;

extern "C" void ReceiveBytes(void) {
    HAL_UART_Receive_IT(&huart2, gRxBuffer.getBuf() + *gRxBuffer.getHead(), 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart != &huart2) {
        return;
    }

    gRxBuffer.advance_head();
    ReceiveBytes();
}
