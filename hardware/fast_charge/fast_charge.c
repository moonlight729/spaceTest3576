#define _POSIX_C_SOURCE 200809L
#include "fast_charge.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Temporary read sources.  bq2579x provides charger online state; cw221X-bat
 * currently provides available voltage/current values.  The final board
 * software will expose a file interface for real charging-input measurements.
 * Change these readers then; do not change fast_charge_run_test() semantics.
 */
#define CHARGER_STATUS_PATH "/sys/class/power_supply/bq2579x-charger/status"
#define BATTERY_PATH "/sys/class/power_supply/cw221X-bat"

static int read_int(const char *directory, const char *name, int *value)
{
    char path[256];
    FILE *file;
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    file = fopen(path, "r");
    if (file == NULL) return -1;
    if (fscanf(file, "%d", value) != 1) { fclose(file); errno = EIO; return -1; }
    fclose(file);
    return 0;
}

static int read_text(const char *path, char *value, size_t size)
{
    FILE *file;
    if (path == NULL || value == NULL || size == 0) return -1;
    file = fopen(path, "r");
    if (file == NULL || fgets(value, size, file) == NULL) {
        if (file != NULL) fclose(file);
        return -1;
    }
    fclose(file);
    value[strcspn(value, "\r\n")] = '\0';
    return 0;
}

static void set_message(struct fast_charge_result *result, int code, const char *message)
{
    size_t length;
    result->error_code = code;
    length = strnlen(message, sizeof(result->message) - 1);
    memcpy(result->message, message, length);
    result->message[length] = '\0';
}

int fast_charge_open(struct fast_charge_device *device)
{
    if (device == NULL) { errno = EINVAL; return -1; }
    device->charger_path = CHARGER_STATUS_PATH;
    device->battery_path = BATTERY_PATH;
    return 0;
}

void fast_charge_close(struct fast_charge_device *device)
{
    if (device != NULL) { device->charger_path = NULL; device->battery_path = NULL; }
}

int fast_charge_read(struct fast_charge_device *device, struct fast_charge_result *result)
{
    char status[32];
    int voltage_uv, current_ua;
    if (device == NULL || result == NULL || device->charger_path == NULL || device->battery_path == NULL) { errno = EINVAL; return -1; }
    memset(result, 0, sizeof(*result));
    if (read_text(device->charger_path, status, sizeof(status)) != 0 ||
        read_int(device->battery_path, "voltage_now", &voltage_uv) != 0 ||
        read_int(device->battery_path, "current_now", &current_ua) != 0) {
        set_message(result, 4400, "Unable to read charger or battery sysfs data");
        return -1;
    }
    result->charger_online = strcmp(status, "Charging") == 0;
    result->voltage_mv = voltage_uv / 1000;
    /* power_supply current_now is signed: positive while charging, negative while discharging. */
    result->current_ma = current_ua / 1000;
    return 0;
}

int fast_charge_run_test(struct fast_charge_device *device, const struct fast_charge_request *request,
                         struct fast_charge_result *result)
{
    struct timespec delay;
    int elapsed = 0;
    if (device == NULL || request == NULL || result == NULL || request->stable_sample_count < 1) { errno = EINVAL; return -1; }
    delay.tv_sec = request->sample_interval_ms / 1000;
    delay.tv_nsec = (request->sample_interval_ms % 1000) * 1000000L;
    while (elapsed <= request->timeout_ms) {
        if (fast_charge_read(device, result) != 0) return -1;
        if (result->current_ma >= request->current_min_ma &&
            result->current_ma <= request->current_max_ma) {
            if (++result->stable_samples >= request->stable_sample_count) {
                set_message(result, 0, "Charge current is in range; battery voltage sampled");
                return 0;
            }
        } else {
            result->stable_samples = 0;
        }
        nanosleep(&delay, NULL);
        elapsed += request->sample_interval_ms;
    }
    set_message(result, 4401, "Charge current did not reach the configured range");
    return -1;
}
