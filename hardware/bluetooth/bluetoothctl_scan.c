#define _GNU_SOURCE
#include "bluetoothctl_scan.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>

#define LINE_SIZE 512

static void set_error(struct bluetooth_result *result, int code, const char *message, const char *reason)
{
    size_t length = strnlen(message, sizeof(result->error_message) - 1);
    size_t reason_length = strnlen(reason, sizeof(result->failure_reason) - 1);
    result->error_code = code;
    memcpy(result->error_message, message, length);
    result->error_message[length] = '\0';
    memcpy(result->failure_reason, reason, reason_length);
    result->failure_reason[reason_length] = '\0';
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

static void update_best_seen(struct bluetooth_result *result, const char *name, const char *mac, int rssi)
{
    if (name[0] == '\0' || mac[0] == '\0') return;
    if (result->best_seen_name[0] == '\0' || rssi > result->best_seen_rssi) {
        snprintf(result->best_seen_name, sizeof(result->best_seen_name), "%s", name);
        snprintf(result->best_seen_mac, sizeof(result->best_seen_mac), "%s", mac);
        result->best_seen_rssi = rssi;
    }
}

static bool parse_rssi_line(const char *line, char *mac, size_t mac_size, int *rssi)
{
    const char *rssi_text = strstr(line, "RSSI:");
    const char *device = strstr(line, "Device ");
    const char *paren;
    (void)mac_size;
    if (rssi_text == NULL || device == NULL) return false;
    device += strlen("Device ");
    if (sscanf(device, "%31s", mac) != 1) return false;
    paren = strchr(rssi_text, '(');
    if (paren != NULL && sscanf(paren, "(%d)", rssi) == 1) return true;
    if (sscanf(rssi_text, "RSSI: %d", rssi) == 1) return true;
    return false;
}
int bluetoothctl_scan_target(const struct bluetooth_request *request,
                             struct bluetooth_result *result)
{
    FILE *stream;
    char command[128], line[LINE_SIZE];
    char current_mac[32] = "";
    char current_name[128] = "";
    bool current_is_target = false;
    int seconds, status;

    if (request == NULL || result == NULL || request->target_name == NULL ||
        request->timeout_ms <= 0) { errno = EINVAL; return -1; }
    memset(result, 0, sizeof(*result));
    result->rssi = -127;
    result->matched_rssi = -127;
    result->best_seen_rssi = -127;
    if (bluetoothctl_health() != 0) {
        set_error(result, 4200, "Bluetooth controller is unavailable", "bluetoothctl_error");
        return -1;
    }
    seconds = (request->timeout_ms + 999) / 1000;
    snprintf(command, sizeof(command), "bluetoothctl --timeout %d scan on 2>&1", seconds);
    stream = popen(command, "r");
    if (stream == NULL) { set_error(result, 4201, "Unable to start bluetoothctl scan", "bluetoothctl_error"); return -1; }
    while (fgets(line, sizeof(line), stream) != NULL) {
        char mac[32] = "", name[128] = "";
        int rssi;
        if (parse_device_line(line, mac, sizeof(mac), name, sizeof(name))) {
            snprintf(current_mac, sizeof(current_mac), "%s", mac);
            snprintf(current_name, sizeof(current_name), "%s", name);
            current_is_target = strcmp(name, request->target_name) == 0;
            if (current_is_target) {
                snprintf(result->name, sizeof(result->name), "%s", name);
                snprintf(result->mac, sizeof(result->mac), "%s", mac);
            }
        }
        if (parse_rssi_line(line, mac, sizeof(mac), &rssi)) {
            if (current_is_target &&
                result->mac[0] != '\0' &&
                current_mac[0] != '\0' &&
                strcmp(mac, result->mac) == 0 &&
                strcmp(mac, current_mac) == 0) {
                result->matched_rssi = rssi;
                result->rssi = rssi;
                update_best_seen(result, result->name, result->mac, rssi);
                if (rssi >= request->min_rssi) result->found = true;
            } else if (current_mac[0] != '\0' &&
                       current_name[0] != '\0' &&
                       strcmp(mac, current_mac) == 0) {
                update_best_seen(result, current_name, current_mac, rssi);
            }
        }
        if (strstr(line, "Device ") != NULL && strstr(line, "RSSI:") != NULL) {
            char seen_mac[32] = "", seen_name[128] = "";
            int seen_rssi;
            if (parse_device_line(line, seen_mac, sizeof(seen_mac), seen_name, sizeof(seen_name)) &&
                parse_rssi_line(line, seen_mac, sizeof(seen_mac), &seen_rssi)) {
                update_best_seen(result, seen_name, seen_mac, seen_rssi);
            }
        }
    }
    status = pclose(stream);
    if (result->found) return 0;
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        set_error(result, 4202, "bluetoothctl scan failed", "bluetoothctl_error");
    } else if (result->mac[0] != '\0') {
        set_error(result, 4203, "Target found but RSSI is below threshold", "target_found_but_rssi_low");
    } else {
        set_error(result, 4204, "Target Bluetooth name was not found", "target_not_found");
    }
    return -1;
}
