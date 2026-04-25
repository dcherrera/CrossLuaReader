/**
 * @file bridge_battery.cpp
 * @brief C++ bridge wrapping BatteryMonitor SDK class for C HAL access.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include <BatteryMonitor.h>

#include <cstddef>

extern "C" {

static BatteryMonitor *battery = nullptr;

/**
 * Initialize the battery monitor.
 *
 * @param adc_pin GPIO pin for battery ADC (X4: pin 0)
 * @param divider Voltage divider multiplier (X4: 2.0)
 */
void bridge_battery_init(uint8_t adc_pin, float divider) {
    if (!battery) {
        battery = new BatteryMonitor(adc_pin, divider);
    }
}

uint16_t bridge_battery_read_percentage(void) {
    if (!battery) return 0;
    return battery->readPercentage();
}

uint16_t bridge_battery_read_millivolts(void) {
    if (!battery) return 0;
    return battery->readMillivolts();
}

}  /* extern "C" */
