#ifndef SPACETEST3576_FINGERPRINT_H
#define SPACETEST3576_FINGERPRINT_H

#include <stdbool.h>

struct fingerprint_device {
    bool opened;
};

struct fingerprint_result {
    bool passed;
    int error_code;
    char message[128];
};

int fingerprint_open(struct fingerprint_device *device);
int fingerprint_health(struct fingerprint_device *device);
int fingerprint_run_test(struct fingerprint_device *device, struct fingerprint_result *result);
void fingerprint_close(struct fingerprint_device *device);

#endif
