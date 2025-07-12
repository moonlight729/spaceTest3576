#ifndef SPACETEST3576_FAST_CHARGE_H
#define SPACETEST3576_FAST_CHARGE_H

#include <stdbool.h>

struct fast_charge_device {
    /*
     * This module validates the input voltage while the board is charging.
     * The current paths are temporary sysfs sources.  When the board exposes
     * the final file interface, replace only the read implementation and keep
     * this API and the manage-layer judgement unchanged.
     */
    const char *charger_path;
    const char *battery_path;
};

struct fast_charge_request {
    int voltage_min_mv;
    int voltage_max_mv;
    int current_min_ma;
    int current_max_ma;
    int stable_sample_count;
    int sample_interval_ms;
    int timeout_ms;
};

struct fast_charge_result {
    bool charger_online;
    /* Charging input voltage/current values used for the configured range check. */
    int voltage_mv;
    int current_ma;
    int stable_samples;
    int error_code;
    char message[160];
};

int fast_charge_open(struct fast_charge_device *device);
void fast_charge_close(struct fast_charge_device *device);
int fast_charge_read(struct fast_charge_device *device, struct fast_charge_result *result);
/* Samples charging input values and checks them against the upper-PC limits. */
int fast_charge_run_test(struct fast_charge_device *device,
                         const struct fast_charge_request *request,
                         struct fast_charge_result *result);

#endif
