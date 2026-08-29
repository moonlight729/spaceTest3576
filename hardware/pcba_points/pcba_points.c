#define _POSIX_C_SOURCE 200809L
#include "pcba_points.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct point_spec { const char *name; int min_mv; int max_mv; };
static const struct point_spec point_specs[32] = {
    {"VDD_DDR_S0",720,730},{"VDDQ_DDR_S0",505,510},{"MASKROM",1610,1620},{"5V",5000,5100},
    {"TS",2500,2700},{"2V",2200,2300},{"VCC5V0_SYS",5000,5100},{"VCC_1V8_S3",1800,1800},
    {"VCC_3V3_S3",3200,3300},{"GND",0,0},{"VBUS5V0_TYPEC",19000,21000},{"VDD2H_DDR_S3",1050,1050},
    {"RECOVERY",1780,1780},{"GND",0,0},{"VDD_CPU_LIT_S0",710,710},{"VBUS5V0_TYPEC",19000,21000},
    {"VCC_SYS",6000,8950},{"VDD_CPU_BIG_S0",710,710},{"VDD_GPU_S0",0,710},{"VCC-RTC",3300,3300},
    {"VDD_LOGIC_S0",750,750},{"VDD_NPU_S0",0,750},{"VBUSIN_VCC",19000,21000},{"RXD",3300,3300},
    {"TXD",3300,3300},{"BLED",0,2500},{"RLED",0,1100},{"GLED",0,2700},
    {"LEDVDD",4650,4650},{"VBUS1_TYPEC",5000,5000},{"FAN-PWM",3300,3300},{"FG",0,5000}
};

static void set_message(struct pcba_points_result *result, int code, const char *message)
{
    size_t length;
    result->error_code = code;
    length = strnlen(message, sizeof(result->message) - 1);
    memcpy(result->message, message, length);
    result->message[length] = '\0';
}

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int read_text_file(const char *path, char *buffer, size_t buffer_size)
{
    FILE *file;
    size_t used;
    if (path == NULL || buffer == NULL || buffer_size == 0) {
        errno = EINVAL;
        return -1;
    }
    file = fopen(path, "r");
    if (file == NULL) return -1;
    used = fread(buffer, 1, buffer_size - 1, file);
    buffer[used] = '\0';
    fclose(file);
    return used == 0 ? -1 : 0;
}

static int parse_json_int(const char *json, const char *key, int *value)
{
    char pattern[80];
    const char *found;
    const char *colon;
    if (json == NULL || key == NULL || value == NULL) return -1;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    found = strstr(json, pattern);
    if (found == NULL) return -1;
    colon = strchr(found + strlen(pattern), ':');
    if (colon == NULL) return -1;
    return sscanf(colon + 1, "%d", value) == 1 ? 0 : -1;
}

static int parse_voltage_values(const char *json, struct pcba_points_result *result)
{
    const char *cursor = json;
    int count = 0;
    while ((cursor = strstr(cursor, "\"voltageMv\"")) != NULL && count < 32) {
        const char *colon = strchr(cursor, ':');
        int voltage = 0;
        if (colon == NULL || sscanf(colon + 1, "%d", &voltage) != 1) {
            return -1;
        }
        result->points[count].index = count + 1;
        result->points[count].voltage_mv = voltage;
        {
            const struct point_spec *spec = &point_specs[count];
            snprintf(result->points[count].name, sizeof(result->points[count].name), "%s", spec->name);
            result->points[count].min_mv = spec->min_mv;
            result->points[count].max_mv = spec->max_mv;
            result->points[count].passed = voltage >= spec->min_mv && voltage <= spec->max_mv;
        }
        if (result->points[count].passed) {
            result->passed_count++;
        } else {
            result->failed_count++;
        }
        count++;
        cursor = colon + 1;
    }
    result->parsed_count = count;
    return count > 0 ? 0 : -1;
}

int pcba_points_run_test(const struct pcba_points_request *request,
                         struct pcba_points_result *result)
{
    char content[8192];
    int elapsed = 0;
    int file_channel_count = 0;
    int requested_count;

    if (request == NULL || result == NULL || request->record_file == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(result, 0, sizeof(*result));
    snprintf(result->record_file, sizeof(result->record_file), "%s", request->record_file);
    requested_count = request->channel_count <= 0 ? 32 : request->channel_count;
    if (requested_count > 32) requested_count = 32;
    result->channel_count = requested_count;

    while (read_text_file(request->record_file, content, sizeof(content)) != 0) {
        if (elapsed >= request->timeout_ms) {
            set_message(result, 5001, "PCBA test point record file not found");
            return -1;
        }
        sleep_ms(200);
        elapsed += 200;
    }

    if (parse_json_int(content, "channelCount", &file_channel_count) != 0) {
        set_message(result, 5000, "PCBA test point record file is invalid");
        return -1;
    }
    if (file_channel_count < requested_count) {
        set_message(result, 5002, "PCBA test point channel count is not enough");
        return -1;
    }
    if (parse_voltage_values(content, result) != 0) {
        set_message(result, 5000, "PCBA test point voltage values are invalid");
        return -1;
    }
    if (result->parsed_count < requested_count) {
        set_message(result, 5002, "PCBA test point voltage value count is not enough");
        return -1;
    }
    if (result->failed_count > 0) {
        set_message(result, 5003, "PCBA test point voltage is out of range");
        return -1;
    }

    set_message(result, 0, "PCBA test point voltages are in range");
    return 0;
}
