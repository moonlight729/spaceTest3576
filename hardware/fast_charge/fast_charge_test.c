#include "fast_charge.h"

#include <stdio.h>

int main(void)
{
    struct fast_charge_device device;
    struct fast_charge_result result;
    if (fast_charge_open(&device) != 0 || fast_charge_read(&device, &result) != 0) return 1;
    printf("online=%d voltageMv=%d currentMa=%d\n", result.charger_online, result.voltage_mv, result.current_ma);
    fast_charge_close(&device);
    return 0;
}
