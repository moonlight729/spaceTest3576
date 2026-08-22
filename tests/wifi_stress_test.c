#include "wifi_nmcli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int parse_positive(const char *value, int fallback)
{
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    return end != value && *end == '\0' && parsed > 0 && parsed < 3600000 ? (int)parsed : fallback;
}

int main(int argc, char **argv)
{
    const char *ssid = argc > 1 ? argv[1] : "Xiaomi_9994";
    const char *interface_name = argc > 2 ? argv[2] : "wlan0";
    const int rounds = argc > 3 ? parse_positive(argv[3], 10) : 10;
    const int min_rssi = argc > 4 ? (int)strtol(argv[4], NULL, 10) : -40;
    const int interval_ms = argc > 5 ? parse_positive(argv[5], 1000) : 1000;
    struct wifi_device device;
    int passed = 0;

    memset(&device, 0, sizeof(device));
    if (wifi_nmcli_open(&device, interface_name) != 0) {
        fprintf(stderr, "OPEN_FAIL interface=%s\n", interface_name);
        return 2;
    }

    printf("WIFI_STRESS_START ssid=%s interface=%s rounds=%d minRssi=%d intervalMs=%d\n",
           ssid, device.interface_name, rounds, min_rssi, interval_ms);
    for (int round = 1; round <= rounds; ++round) {
        struct wifi_request request = { .ssid = ssid, .scan_timeout_ms = 10000 };
        struct wifi_result result;
        const int rc = wifi_nmcli_scan_signal(&device, &request, &result);
        const int ok = rc == 0 && result.wifi_enabled && result.found && result.rssi >= min_rssi;
        if (ok) passed++;
        printf("ROUND %d/%d %s rc=%d enabled=%s found=%s rssi=%d minRssi=%d scanRetries=%d reason=%s message=%s\n",
               round, rounds, ok ? "PASS" : "FAIL", rc,
               result.wifi_enabled ? "true" : "false", result.found ? "true" : "false",
               result.rssi, min_rssi, result.scan_retry_count,
               result.failure_reason, result.error_message);
        fflush(stdout);
        if (round < rounds) usleep((useconds_t)interval_ms * 1000U);
    }
    wifi_nmcli_close(&device);
    printf("WIFI_STRESS_RESULT %s passed=%d total=%d\n", passed == rounds ? "PASS" : "FAIL", passed, rounds);
    return passed == rounds ? 0 : 1;
}
