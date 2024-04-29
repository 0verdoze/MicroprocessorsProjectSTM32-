#include <charconv>
#include <cstdarg>
#include "command_handler.h"
#include "error_codes.h"
#include "main.h"

extern TIM_HandleTypeDef htim2;

#define TIMER_FREQ (HAL_RCC_GetPCLK1Freq() * 2)

/// @brief helper function to format string, and store it in return buffer
/// @param ret return buffer
/// @param fmt format string (refer to printf for formatting)
/// @param ... arguments
void Printf(bytes& ret, const char* fmt, ...) {
    va_list args, dummy_args;

    va_start(args, fmt);
    va_copy(dummy_args, args);

    int size = vsnprintf(NULL, (unsigned)0, fmt, dummy_args) + 1;
    size_t used = std::size(ret);
    ret.resize(used + static_cast<size_t>(size));

    vsnprintf((char*)ret.data() + used, (unsigned)size, fmt, args);
    ret.pop_back();

    va_end(args);
    va_end(dummy_args);
}

void StartPWM() {
    gDeviceState.isPwmGenerated = true;
    HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_1, gDeviceState.dutyCycles.data(), gDeviceState.dutyCycles.size());
}

void StopPWM() {
    gDeviceState.isPwmGenerated = false;
    HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_1);
}

void SetArr(std::uint32_t arr) {
    HAL_TIM_Base_Stop(&htim2);

    __HAL_TIM_SET_AUTORELOAD(&htim2, arr - 1);
    __HAL_TIM_SET_COUNTER(&htim2, 0);

    HAL_TIM_Base_Start(&htim2);
}

DEF_CMD(CmdPwmOn) {
    if (!gDeviceState.isPwmGenerated) {
        StartPWM();
    }

    Printf(ret, "%s", kPwmOn);
}

DEF_CMD(CmdPwmOff) {
    if (gDeviceState.isPwmGenerated) {
        StopPWM();
    }

    Printf(ret, "%s", kPwmOff);
}

DEF_CMD(CmdSetFreq) {
    auto& arg = args[1];
    std::uint32_t value = 0;

    auto [ptr, ec] = std::from_chars((const char*)arg.data(), (const char*)arg.data() + std::size(arg), value);
    if (ec != std::errc() || ptr != (const char*)arg.data() + std::size(arg)) {
        Printf(ret, "%s", kInvalidArgument);
        return;
    }

    bool restoreGeneration = false;

    std::uint32_t arr = TIMER_FREQ / value;
    if ((arr == 0) || (value == 0)) {
        Printf(ret, "%s", kInvalidFrequency);
        return;
    }

    if (gDeviceState.isPwmGenerated) {
        StopPWM();
        restoreGeneration = true;
    }

    SetArr(arr);
    // update duty cycles
    for (size_t i = 0; i < std::size(gDeviceState.userDutyCycles); i++) {
        gDeviceState.dutyCycles[i] = static_cast<std::uint64_t>(gDeviceState.userDutyCycles[i]) * arr / 100;
    }

    if (restoreGeneration) {
        StartPWM();
    }

    Printf(ret, "%s %d", kFreqChanged, value);
}

DEF_CMD(CmdSetDutyCycles) {
    std::vector<std::uint32_t> params;
    for (size_t i = 1; i < std::size(args); i++) {
        auto& arg = args[i];
        std::uint32_t value = 0;

        auto [ptr, ec] = std::from_chars((const char*)arg.data(), (const char*)arg.data() + std::size(arg), value);
        if (ec != std::errc() || ptr != (const char*)arg.data() + std::size(arg)) {
            Printf(ret, "%s", kInvalidArgument);
            return;
        }

        params.push_back(value);
    }

    std::uint32_t arr = htim2.Instance->ARR + 1;

    std::vector<std::uint32_t> dutyCycles;
    std::vector<std::uint8_t> userDutyCycles;

    bool invalidDutyCycle = std::any_of(std::begin(params), std::end(params), [&](auto dutyCycle) {
        userDutyCycles.push_back(dutyCycle);
        dutyCycles.push_back(static_cast<std::uint64_t>(dutyCycle) * arr / 100);

        return (dutyCycle < 0) | (dutyCycle > 100);;
    });

    if (!invalidDutyCycle) {
        if (gDeviceState.isPwmGenerated) {
            StopPWM();
            gDeviceState.dutyCycles = std::move(dutyCycles);
            StartPWM();
        } else {
            gDeviceState.dutyCycles = std::move(dutyCycles);
        }
        gDeviceState.userDutyCycles = std::move(userDutyCycles);

        Printf(ret, "%s", kDutyCyclesChanged);
        std::for_each(++std::begin(args), std::end(args), [&](auto param) {
            ret.push_back(' ');
            std::copy(std::begin(param), std::end(param), std::back_inserter(ret));
        });
    } else {
        Printf(ret, "%s", kInvalidDutyCycle);
    }
}

DEF_CMD(CmdStatus) {
    Printf(ret, "%s", kStatusResp);

    Printf(ret, " %c", '0' + gDeviceState.isPwmGenerated);

    // recalc frequency
    auto arr = htim2.Instance->ARR + 1;
    std::uint32_t freq = TIMER_FREQ / arr;
    Printf(ret, " %d", freq);

    auto& dutyCycles = gDeviceState.dutyCycles;
    std::for_each(std::begin(dutyCycles), std::end(dutyCycles), [&](auto cnt) {
        std::uint32_t dutyCycle = static_cast<std::uint64_t>(cnt) * 100 / arr;
        Printf(ret, " %d", dutyCycle);
    });
}

extern "C" void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
    HAL_TIM_PWM_Stop_DMA(htim, TIM_CHANNEL_1);

    if (gDeviceState.isPwmGenerated) {
        HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_1, gDeviceState.dutyCycles.data(), gDeviceState.dutyCycles.size());
    }
}

