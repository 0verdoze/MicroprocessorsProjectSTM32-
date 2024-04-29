/*
 * error_codes.h
 *
 *  Created on: Jan 13, 2024
 *      Author: dacja
 */

#ifndef ERROR_CODES_H_
#define ERROR_CODES_H_

#include <string_view>

/// List of error codes (messages) that can be returned to caller device

constexpr std::string_view kUnknownCommand = "UNKNOWN_COMMAND";
constexpr std::string_view kInvalidArgument = "INVALID_ARGUMENT";
constexpr const char* kPwmOn = "PWM_ON";
constexpr const char* kPwmOff = "PWM_OFF";
constexpr const char* kFreqChanged = "FREQ_CHANGED";
constexpr const char* kDutyCyclesChanged = "DUTY_CYCLES_CHANGED";
constexpr const char* kInvalidFrequency = "INVALID_FREQUENCY";
constexpr const char* kInvalidDutyCycle = "INVALID_DUTY_CYCLE";
constexpr const char* kStatusResp = "STATUS_RESP";

#endif /* ERROR_CODES_H_ */
