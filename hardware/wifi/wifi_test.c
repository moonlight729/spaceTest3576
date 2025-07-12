#include "wifi_nmcli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct wifi_device device;
    struct wifi_request request;
    struct wifi_result result;
    int rc;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <active-ssid> <router-ip>\n", argv[0]);
        return 2;
    }
    memset(&request, 0, sizeof(request));
    request.ssid = argv[1];
    request.router_ip = argv[2];
    request.ping_count = 4;
    request.timeout_ms = 15000;
    request.reuse_current_connection = true;
    if (wifi_nmcli_open(&device, "wlan0") != 0) { perror("wifi_nmcli_open"); return 1; }
    rc = wifi_nmcli_run_test(&device, &request, &result);
    wifi_nmcli_close(&device);
    if (rc != 0) {
        fprintf(stderr, "FAIL code=%d message=%s\n", result.error_code, result.error_message);
        return 1;
    }
    printf("PASS ssid=%s ip=%s router=%s pingCount=%d avgDelayMs=%d\n",
           request.ssid, result.ip, request.router_ip,
           result.completed_ping_count, result.avg_delay_ms);
    return 0;
}
