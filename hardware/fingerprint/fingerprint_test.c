#include "fingerprint.h"

#include <stdio.h>

int main(void)
{
    struct fingerprint_device device = {0};
    struct fingerprint_result result;
    if (fingerprint_open(&device) != 0 || fingerprint_run_test(&device, &result) != 0) return 1;
    printf("%s: %s\n", result.passed ? "PASS" : "FAIL", result.message);
    fingerprint_close(&device);
    return result.passed ? 0 : 1;
}
