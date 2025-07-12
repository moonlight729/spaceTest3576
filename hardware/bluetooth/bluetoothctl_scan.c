#define _GNU_SOURCE
#include "bluetoothctl_scan.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define LINE_SIZE 512

static void set_error(struct bluetooth_result *result, int code, const char *message)
{
    size_t length = strnlen(message, sizeof(result->error_message) - 1);
    result->error_code = code;
    memcpy(result->error_message, message, length);
    result->error_message[length] = '\0';
}

static int command_exit_status(const char *command)
{
    int status = system(command);
    return status >= 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int bluetoothctl_health(void)
{
    /* show returns non-zero when BlueZ has no default controller. */
    return command_exit_status("bluetoothctl show >/dev/null 2>&1") == 0 ? 0 : -1;
}

static bool parse_device_line(const char *line, char *mac, size_t mac_size,
                              char *name, size_t name_size)
{
    const char *device = strstr(line, "Device ");
    const char *name_start;
    (void)mac_size;
    if (device == NULL) return false;
    device += strlen("Device ");
    if (sscanf(device, "%31s", mac) != 1) return false;
    name_start = device + strcspn(device, " \t");
    while (*name_start == ' ' || *name_start == '\t') ++name_start;
    if (*name_start == '\0') return false;
    snprintf(name, name_size, "%s", name_start);
    name[strcspn(name, "\r\n")] = '\0';
    return mac[0] != '\0' && name[0] != '\0';
}

int bluetoothctl_scan_target(const struct bluetooth_request *request,
                             struct bluetooth_result *result)
{
    FILE *stream;
    char command[128], line[LINE_SIZE], candidate_mac[32] = "";
    bool candidate = false;
    int seconds, status;

    if (request == NULL || result == NULL || request->target_name == NULL ||
        request->timeout_ms <= 0) { errno = EINVAL; return -1; }
    memset(result, 0, sizeof(*result));
    result->rssi = -127;
    if (bluetoothctl_health() != 0) {
        set_error(result, 4200, "Bluetooth controller is unavailable");
        return -1;
    }
    seconds = (request->timeout_ms + 999) / 1000;
    snprintf(command, sizeof(command), "bluetoothctl --timeout %d scan on 2>&1", seconds);
    stream = popen(command, "r");
    if (stream == NULL) { set_error(result, 4201, "Unable to start bluetoothctl scan"); return -1; }
    while (fgets(line, sizeof(line), stream) != NULL) {
        char mac[32] = "", name[128] = "";
        char *rssi_text;
        int rssi;
        if (parse_device_line(line, mac, sizeof(mac), name, sizeof(name)) &&
            strcmp(name, request->target_name) == 0) {
            candidate = true;
            snprintf(candidate_mac, sizeof(candidate_mac), "%s", mac);
            snprintf(result->name, sizeof(result->name), "%s", name);
            snprintf(result->mac, sizeof(result->mac), "%s", mac);
        }
        rssi_text = strstr(line, "RSSI:");
        if (candidate && rssi_text != NULL && strstr(line, candidate_mac) != NULL &&
            sscanf(rssi_text, "RSSI: %d", &rssi) == 1) {
            result->rssi = rssi;
            if (rssi >= request->min_rssi) result->found = true;
        }
    }
    status = pclose(stream);
    if (result->found) return 0;
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        set_error(result, 4202, "bluetoothctl scan failed");
    } else if (candidate) {
        set_error(result, 4203, "Target found but RSSI is below threshold");
    } else {
        set_error(result, 4204, "Target Bluetooth name was not found");
    }
    return -1;
}
