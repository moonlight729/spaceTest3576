#define _POSIX_C_SOURCE 200809L
#include "usb3_file_check.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void set_message(struct usb3_result *result, int code, const char *message)
{
    size_t length;
    result->error_code = code;
    length = strnlen(message, sizeof(result->message) - 1);
    memcpy(result->message, message, length);
    result->message[length] = '\0';
}

static int read_int_file(const char *path, int *value)
{
    FILE *file;
    if (path == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    file = fopen(path, "r");
    if (file == NULL) return -1;
    if (fscanf(file, "%d", value) != 1) {
        fclose(file);
        errno = EIO;
        return -1;
    }
    fclose(file);
    return 0;
}

int usb3_open(struct usb3_device *device)
{
    if (device == NULL) {
        errno = EINVAL;
        return -1;
    }
    device->present_path = NULL;
    device->speed_path = NULL;
    device->rw_check_path = NULL;
    return 0;
}

void usb3_close(struct usb3_device *device)
{
    if (device != NULL) {
        device->present_path = NULL;
        device->speed_path = NULL;
        device->rw_check_path = NULL;
    }
}

int usb3_configure_paths(struct usb3_device *device,
                         const char *present_path,
                         const char *speed_path,
                         const char *rw_check_path)
{
    if (device == NULL) {
        errno = EINVAL;
        return -1;
    }
    device->present_path = present_path;
    device->speed_path = speed_path;
    device->rw_check_path = rw_check_path;
    return 0;
}

int usb3_read(struct usb3_device *device, struct usb3_result *result)
{
    int present;
    int speed_mbps;
    int rw_ok = 0;

    if (device == NULL || result == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(result, 0, sizeof(*result));
    /*
     * Current framework reads only the latest USB state.  The final board file
     * interface should expose USB2.0 and USB3.0 plug history, including four
     * complete insertion/removal cycles for each mode.  When those files exist,
     * extend this reader to parse the history and let usb3_run_test() judge the
     * four-cycle requirement.  Until then, an unconfigured device returns 4500.
     */
    if (device->present_path == NULL || device->speed_path == NULL) {
        set_message(result, 4500, "USB3 file interface is not configured");
        errno = ENODEV;
        return -1;
    }
    if (read_int_file(device->present_path, &present) != 0 ||
        read_int_file(device->speed_path, &speed_mbps) != 0) {
        set_message(result, 4501, "Unable to read USB3 state files");
        return -1;
    }
    if (device->rw_check_path != NULL) {
        if (read_int_file(device->rw_check_path, &rw_ok) != 0) {
            set_message(result, 4502, "Unable to read USB3 read/write check file");
            return -1;
        }
        result->rw_checked = rw_ok != 0;
    }
    result->present = present != 0;
    result->speed_mbps = speed_mbps;
    set_message(result, 0, "USB3 state read successfully");
    return 0;
}

int usb3_run_test(struct usb3_device *device,
                  const struct usb3_request *request,
                  struct usb3_result *result)
{
    if (device == NULL || request == NULL || result == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (usb3_read(device, result) != 0) return -1;
    if (!result->present) {
        set_message(result, 4503, "USB3 device is not present");
        return -1;
    }
    if (result->speed_mbps < request->expected_min_speed_mbps) {
        set_message(result, 4504, "USB3 negotiated speed is below the configured limit");
        return -1;
    }
    if (request->require_rw_check && !result->rw_checked) {
        set_message(result, 4505, "USB3 read/write check did not pass");
        return -1;
    }
    set_message(result, 0, "USB3 check passed");
    return 0;
}
