#include "platform/KeyRepeat.h"

#include <windows.h>

namespace workpane::platform {

// The system names the delay as a step from a quarter of a second to a whole second and the speed as a step from about 2.5 to about 30 repeats a second.
std::optional<KeyRepeat> KeyRepeat::system() {
    constexpr float delayStep = 0.25F;
    constexpr float slowestRate = 2.5F;
    constexpr float fastestRate = 30.0F;
    constexpr DWORD fastestSpeed = 31;
    int delay = 0;
    DWORD speed = 0;

    if (SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delay, 0) == FALSE || SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0) == FALSE) {
        return std::nullopt;
    }

    const float rate = slowestRate + static_cast<float>(speed) * (fastestRate - slowestRate) / static_cast<float>(fastestSpeed);
    return KeyRepeat{static_cast<float>(delay + 1) * delayStep, 1.0F / rate};
}

} // namespace workpane::platform
