#include "bluetoothctl_scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct bluetooth_request request;
    struct bluetooth_result result;
    if (argc != 4) {
        fprintf(stderr, "usage: %s <target-name> <timeout-ms> <min-rssi>\n", argv[0]);
        return 2;
    }
    memset(&request, 0, sizeof(request));
    request.target_name = argv[1];
    request.timeout_ms = atoi(argv[2]);
    request.min_rssi = atoi(argv[3]);
    if (bluetoothctl_scan_target(&request, &result) != 0) {
        fprintf(stderr, "FAIL code=%d message=%s\n", result.error_code, result.error_message);
        return 1;
    }
    printf("PASS name=%s mac=%s rssi=%d\n", result.name, result.mac, result.rssi);
    return 0;
}
