#pragma once

/// Function declared here, are exposed to C api
/// and therefore can be called from C

/// @brief Enable (start) receiving data via USART
void ReceiveBytes(void);

/// @brief Check for new command, and execute it
void HandlePendingCommands(void);

/// @brief Send a string (message) to target device
/// @param receiver ID of target device
/// @param s Message that device should receive
void SendString(uint8_t receiver, const char* s);
