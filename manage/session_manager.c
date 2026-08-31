#define _POSIX_C_SOURCE 200809L
#include "session_manager.h"

#include "../protocol/protocol.h"
#include "../storage/board_state.h"
#include "../tests/test_runner.h"
#include "../hardware/pressure/pressure_stress.h"
#include "../hardware/wifi/wifi_nmcli.h"
#include "../hardware/ethernet/ethernet_nmcli.h"
#include "../hardware/bluetooth/bluetoothctl_scan.h"
#include "../hardware/pressure/pressure_peripheral.h"
#include "../hardware/camera/camera_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <time.h>

static pid_t pressure_cpu_pid = -1;
static pid_t pressure_memory_pid = -1;
static time_t pressure_load_started_at = 0;
static int pressure_load_error_count = 0;

static int send_failure(int fd, const char *session_id, int code, const char *message);
static int send_ok_response(int fd, const char *session_id, const char *message);

static int control_gen1_app_service(const char *action)
{
    char command[128];
    int rc;
    if (action == NULL || (strcmp(action, "stop") != 0 && strcmp(action, "start") != 0)) return -1;
    snprintf(command, sizeof(command), "systemctl %s gen1-app.service", action);
    rc = system(command);
    if (rc == 1280) {
        fprintf(stderr, "[SERVICE] gen1-app.service not installed; skip %s\n", action);
        return 0;
    }
    fprintf(stderr, "[SERVICE] %s rc=%d\n", command, rc);
    return rc == 0 ? 0 : -1;
}

struct pressure_worker_args { int is_cpu; struct pressure_stress_result *result; int rc; };
static void *run_pressure_worker(void *argument)
{
    struct pressure_worker_args *args = argument;
    args->rc = args->is_cpu ? pressure_run_cpu(4, 60, args->result) : pressure_run_memory(512, 60, args->result);
    return NULL;
}

static int handle_pressure_start(int fd, const struct protocol_request *request)
{
    struct pressure_stress_result cpu = {0};
    struct pressure_stress_result memory = {0};
    char data[256];
    char line[512];
    struct pressure_worker_args cpu_args = { .is_cpu = 1, .result = &cpu, .rc = -1 };
    struct pressure_worker_args memory_args = { .is_cpu = 0, .result = &memory, .rc = -1 };
    pthread_t cpu_thread;
    pthread_t memory_thread;
    int cpu_rc;
    int memory_rc;
    if (pthread_create(&cpu_thread, NULL, run_pressure_worker, &cpu_args) != 0 ||
        pthread_create(&memory_thread, NULL, run_pressure_worker, &memory_args) != 0) {
        return send_failure(fd, request->session_id, 6002, "Unable to start CPU/memory pressure workers");
    }
    pthread_join(cpu_thread, NULL);
    pthread_join(memory_thread, NULL);
    cpu_rc = cpu_args.rc;
    memory_rc = memory_args.rc;
    snprintf(data, sizeof(data), "{\"cpu\":{\"exitCode\":%d,\"durationSec\":%d,\"errorCount\":%d},\"memory\":{\"exitCode\":%d,\"durationSec\":%d,\"errorCount\":%d}}", cpu.exit_code, cpu.duration_sec, cpu.error_count, memory.exit_code, memory.duration_sec, memory.error_count);
    protocol_build_response_envelope(line, sizeof(line), request->session_id, cpu_rc == 0 && memory_rc == 0 ? 0 : 6001, cpu_rc == 0 && memory_rc == 0 ? "Pressure CPU and memory run completed" : "Pressure CPU or memory run failed", data);
    return protocol_write_line(fd, line);
}

static int handle_pressure_metric(int fd, const struct protocol_request *request, int is_cpu)
{
    struct pressure_stress_result result = {0};
    char data[192]; char line[448];
    int rc = is_cpu ? pressure_run_cpu(4, 60, &result) : pressure_run_memory(512, 60, &result);
    snprintf(data, sizeof(data), "{\"exitCode\":%d,\"durationSec\":%d,\"errorCount\":%d}", result.exit_code, result.duration_sec, result.error_count);
    protocol_build_response_envelope(line, sizeof(line), request->session_id, rc == 0 ? 0 : 6001,
        rc == 0 ? (is_cpu ? "CPU pressure completed" : "Memory pressure completed") : "Pressure metric failed", data);
    return protocol_write_line(fd, line);
}

static int pressure_worker_alive(pid_t *pid)
{
    int status;
    if (*pid <= 0) return 0;
    if (waitpid(*pid, &status, WNOHANG) == *pid) {
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) pressure_load_error_count++;
        *pid = -1;
        return 0;
    }
    return kill(*pid, 0) == 0;
}

static pid_t start_pressure_process(const char *command)
{
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    return pid;
}

static int send_pressure_load_status(int fd, const struct protocol_request *request, const char *message)
{
    int cpu_alive = pressure_worker_alive(&pressure_cpu_pid);
    int memory_alive = pressure_worker_alive(&pressure_memory_pid);
    int running = cpu_alive && memory_alive;
    long elapsed = pressure_load_started_at == 0 ? 0 : (long)(time(NULL) - pressure_load_started_at);
    char data[384]; char line[640];
    snprintf(data, sizeof(data), "{\"running\":%s,\"elapsedSec\":%ld,\"cpuWorkers\":4,\"memoryMiB\":512,\"errorCount\":%d,\"detail\":\"cpu=%s,memory=%s\"}",
        running ? "true" : "false", elapsed, pressure_load_error_count, cpu_alive ? "running" : "stopped", memory_alive ? "running" : "stopped");
    protocol_build_response_envelope(line, sizeof(line), request->session_id, running || pressure_load_started_at == 0 ? 0 : 6003, message, data);
    return protocol_write_line(fd, line);
}

static int handle_pressure_load_start(int fd, const struct protocol_request *request)
{
    if (!pressure_worker_alive(&pressure_cpu_pid)) pressure_cpu_pid = start_pressure_process("exec stress-ng --cpu 4 --metrics-brief >/dev/null 2>&1");
    if (!pressure_worker_alive(&pressure_memory_pid)) pressure_memory_pid = start_pressure_process("exec stress-ng --vm 1 --vm-bytes 512M --vm-keep --metrics-brief >/dev/null 2>&1");
    if (pressure_cpu_pid <= 0 || pressure_memory_pid <= 0) return send_failure(fd, request->session_id, 6002, "Unable to start persistent CPU/memory pressure workers");
    pressure_load_started_at = time(NULL);
    pressure_load_error_count = 0;
    return send_pressure_load_status(fd, request, "Persistent CPU/memory pressure started");
}

static int handle_pressure_load_stop(int fd, const struct protocol_request *request)
{
    if (pressure_cpu_pid > 0) kill(pressure_cpu_pid, SIGTERM);
    if (pressure_memory_pid > 0) kill(pressure_memory_pid, SIGTERM);
    pressure_cpu_pid = -1; pressure_memory_pid = -1;
    return send_pressure_load_status(fd, request, "Persistent CPU/memory pressure stopped");
}

static int handle_pressure_wifi(int fd, const struct protocol_request *request, const struct app_config *config)
{
    struct wifi_device device;
    struct wifi_result result = {0};
    struct wifi_request scan = { .ssid = config->wifi_ssid, .scan_timeout_ms = 10000 };
    char data[512];
    char line[768];
    int rc;
    result.rssi = -127;
    if (wifi_nmcli_open(&device, NULL) != 0) return send_failure(fd, request->session_id, 6101, "Unable to open Wi-Fi interface");
    rc = wifi_nmcli_scan_signal(&device, &scan, &result);
    snprintf(data, sizeof(data), "{\"interfaceName\":\"%s\",\"found\":%s,\"rssi\":%d,\"scanRetryCount\":%d,\"failureReason\":\"%s\"}",
             device.interface_name, result.found ? "true" : "false", result.rssi, result.scan_retry_count, result.failure_reason);
    wifi_nmcli_close(&device);
    protocol_build_response_envelope(line, sizeof(line), request->session_id,
        rc == 0 && result.found ? 0 : (result.error_code == 0 ? 6102 : result.error_code),
        rc == 0 && result.found ? "Wi-Fi pressure scan completed" : result.error_message, data);
    return protocol_write_line(fd, line);
}

static int handle_pressure_ethernet(int fd, const struct protocol_request *request, const struct app_config *config)
{
    struct ethernet_request test = { .interface_name = "end0", .router_ip = config->wifi_router_ip, .ping_count = 4, .timeout_ms = 15000 };
    struct ethernet_result result = {0};
    char data[512];
    char line[768];
    int rc = ethernet_nmcli_run_test(&test, &result);
    snprintf(data, sizeof(data), "{\"interfaceName\":\"%s\",\"ip\":\"%s\",\"routerIp\":\"%s\",\"linkUp\":%s,\"ipAcquired\":%s,\"pingOk\":%s,\"pingCount\":%d,\"avgDelayMs\":%d,\"failureReason\":\"%s\"}",
             result.interface_name, result.ip, result.router_ip, result.link_up ? "true" : "false", result.ip_acquired ? "true" : "false", result.ping_ok ? "true" : "false", result.completed_ping_count, result.avg_delay_ms, result.failure_reason);
    protocol_build_response_envelope(line, sizeof(line), request->session_id, rc == 0 ? 0 : (result.error_code == 0 ? 6201 : result.error_code), rc == 0 ? "Ethernet pressure check completed" : result.message, data);
    return protocol_write_line(fd, line);
}

static int handle_pressure_bluetooth(int fd, const struct protocol_request *request, const struct app_config *config)
{
    struct bluetooth_request scan = { .target_name = config->bluetooth_target_name, .timeout_ms = 10000, .min_rssi = config->bluetooth_min_rssi };
    struct bluetooth_result result = {0};
    char data[512];
    char line[768];
    int rc = bluetoothctl_scan_target(&scan, &result);
    snprintf(data, sizeof(data), "{\"found\":%s,\"name\":\"%s\",\"mac\":\"%s\",\"rssi\":%d,\"failureReason\":\"%s\"}",
             result.found ? "true" : "false", result.name, result.mac, result.rssi, result.failure_reason);
    protocol_build_response_envelope(line, sizeof(line), request->session_id, rc == 0 ? 0 : (result.error_code == 0 ? 6301 : result.error_code), rc == 0 ? "Bluetooth pressure scan completed" : result.error_message, data);
    return protocol_write_line(fd, line);
}

/* USB media and kernel drivers can fail independently.  Do not allow a fault in that
 * path to terminate the TCP service (and with it the continuous CPU/memory workers). */
struct usb_pressure_worker_response { int rc; struct pressure_peripheral_result result; };
static int pressure_check_usb_isolated(struct pressure_peripheral_result *result)
{
    int pipe_fd[2]; pid_t pid; struct usb_pressure_worker_response response; fd_set readable;
    struct timeval timeout = { .tv_sec = 30, .tv_usec = 0 };
    ssize_t received;
    if (pipe(pipe_fd) != 0) return -1;
    pid = fork();
    if (pid == 0) {
        ssize_t written;
        close(pipe_fd[0]);
        memset(&response, 0, sizeof(response));
        response.rc = pressure_check_usb_storage(&response.result);
        written = write(pipe_fd[1], &response, sizeof(response));
        close(pipe_fd[1]);
        _exit(written == (ssize_t)sizeof(response) ? 0 : 1);
    }
    close(pipe_fd[1]);
    if (pid < 0) { close(pipe_fd[0]); return -1; }
    FD_ZERO(&readable); FD_SET(pipe_fd[0], &readable);
    if (select(pipe_fd[0] + 1, &readable, NULL, NULL, &timeout) <= 0) {
        kill(pid, SIGKILL); waitpid(pid, NULL, 0); close(pipe_fd[0]);
        memset(result, 0, sizeof(*result)); result->error_code = 6605;
        snprintf(result->detail, sizeof(result->detail), "USB pressure worker timed out");
        return -1;
    }
    received = read(pipe_fd[0], &response, sizeof(response));
    close(pipe_fd[0]); waitpid(pid, NULL, 0);
    if (received != (ssize_t)sizeof(response)) {
        memset(result, 0, sizeof(*result)); result->error_code = 6606;
        snprintf(result->detail, sizeof(result->detail), "USB pressure worker exited unexpectedly");
        return -1;
    }
    *result = response.result;
    return response.rc;
}

static int handle_pressure_peripheral(int fd, const struct protocol_request *request, const char *item_id)
{
    struct pressure_peripheral_result result;
    char data[320];
    char line[512];
    int rc = strcmp(item_id, "fan") == 0
        ? pressure_check_fan("/sys/class/hwmon/hwmon12/pwm1", "/sys/class/hwmon/hwmon12/tach_rpm", &result)
        : strcmp(item_id, "hdmi") == 0 ? pressure_check_hdmi(&result) : pressure_check_usb_isolated(&result);
    snprintf(data, sizeof(data), "{\"value\":%d,\"active\":%s,\"detail\":\"%s\"}", result.value, result.active ? "true" : "false", result.detail);
    protocol_build_response_envelope(line, sizeof(line), request->session_id, rc == 0 ? 0 : result.error_code,
        rc == 0 ? "Pressure peripheral check completed" : result.detail, data);
    return protocol_write_line(fd, line);
}

static int handle_pressure_storage(int fd, const struct protocol_request *request, const struct app_config *config, const char *item_id)
{
    struct pressure_peripheral_result result;
    char data[320]; char line[512];
    const char *directory = strcmp(item_id, "emmc") == 0 ? "/userdata/factory_test" : config->tf_mount_point;
    int rc = pressure_check_file_storage(directory, strcmp(item_id, "emmc") == 0 ? "eMMC" : "TF", &result);
    snprintf(data, sizeof(data), "{\"value\":%d,\"active\":%s,\"detail\":\"%s\"}", result.value, result.active ? "true" : "false", result.detail);
    protocol_build_response_envelope(line, sizeof(line), request->session_id, rc == 0 ? 0 : result.error_code, rc == 0 ? "Pressure storage check completed" : result.detail, data);
    return protocol_write_line(fd, line);
}

static int handle_pressure_camera(int fd, const struct protocol_request *request, const struct app_config *config)
{
    struct camera_stream_request camera_request = {
        .device_path = config->camera_device_path,
        .stream_frame_count = 1800,
        .timeout_ms = 3000,
        .require_exposure_interrupt = 0,
        .exposure_counter_path = NULL,
        .exposure_frame_count = 0,
        .require_pwm_pulse = 0,
        .pwm_status_path = NULL,
        .pwm_min_pulse_delta = 0
    };
    struct camera_stream_result result;
    struct timespec started; struct timespec ended;
    long elapsed_ms; int fps; char data[512]; char line[768]; int rc;
    clock_gettime(CLOCK_MONOTONIC, &started);
    rc = camera_stream_run_test(&camera_request, &result);
    clock_gettime(CLOCK_MONOTONIC, &ended);
    elapsed_ms = (ended.tv_sec - started.tv_sec) * 1000L + (ended.tv_nsec - started.tv_nsec) / 1000000L;
    if (elapsed_ms < 1) elapsed_ms = 1;
    fps = result.captured_frames * 1000 / (int)elapsed_ms;
    snprintf(data, sizeof(data), "{\"value\":%d,\"active\":%s,\"detail\":\"%.180s\",\"device\":\"%.100s\",\"capturedFrames\":%d,\"durationMs\":%ld,\"fps\":%d,\"frameTimeoutCount\":%d}",
             result.captured_frames, rc == 0 && result.stream_ok ? "true" : "false",
             result.message, result.device_path, result.captured_frames, elapsed_ms, fps, rc == 0 ? 0 : 1);
    protocol_build_response_envelope(line, sizeof(line), request->session_id, rc == 0 ? 0 : (result.error_code == 0 ? 6801 : result.error_code),
        rc == 0 ? "USB camera pressure stream completed" : result.message, data);
    return protocol_write_line(fd, line);
}

static int send_application_md5(int fd, const char *session_id, const struct app_config *config)
{
    char command[512];
    char line[256];
    char md5[64] = "";
    char data[1024];
    char response[1400];
    FILE *pipe;

    if (config == NULL || config->application_path == NULL || config->application_service == NULL) {
        return send_failure(fd, session_id, 2300, "Application upgrade configuration is unavailable");
    }
    snprintf(command, sizeof(command), "md5sum '%s' 2>/dev/null", config->application_path);
    pipe = popen(command, "r");
    if (pipe == NULL || fgets(line, sizeof(line), pipe) == NULL) {
        if (pipe != NULL) pclose(pipe);
        return send_failure(fd, session_id, 2301, "Unable to calculate application MD5");
    }
    pclose(pipe);
    if (sscanf(line, "%63s", md5) != 1 || strlen(md5) != 32) {
        return send_failure(fd, session_id, 2301, "Invalid application MD5");
    }
    snprintf(data, sizeof(data),
             "{\"appName\":\"spacetest3576\",\"path\":\"%s\",\"md5\":\"%s\",\"service\":\"%s\"}",
             config->application_path, md5, config->application_service);
    protocol_build_response_envelope(response, sizeof(response), session_id, 0,
                                     "Application MD5 loaded", data);
    return protocol_write_line(fd, response);
}

static int send_application_version(int fd, const char *session_id, const struct app_config *config)
{
    char data[512];
    char response[900];
    const char *version = config != NULL && config->application_version != NULL
        ? config->application_version : "";
    if (version[0] == '\0') {
        return send_failure(fd, session_id, 2404, "Application version unavailable");
    }
    snprintf(data, sizeof(data),
             "{\"appName\":\"spacetest3576\",\"version\":\"%s\",\"versionAvailable\":true,\"path\":\"%s\"}",
             version, config->application_path);
    protocol_build_response_envelope(response, sizeof(response), session_id, 0,
                                     "Application version loaded", data);
    return protocol_write_line(fd, response);
}

static int json_get_string_value(const char *json, const char *key, char *buffer, size_t buffer_size)
{
    char pattern[64];
    const char *start;
    const char *end;
    size_t length;
    if (json == NULL || key == NULL || buffer == NULL || buffer_size == 0) return -1;
    buffer[0] = '\0';
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    start = strstr(json, pattern);
    if (start == NULL) return -1;
    start = strchr(start + strlen(pattern), ':');
    if (start == NULL) return -1;
    while (*++start == ' ' || *start == '\t') { }
    if (*start != '"') return -1;
    end = strchr(++start, '"');
    if (end == NULL) return -1;
    length = (size_t)(end - start);
    if (length >= buffer_size) length = buffer_size - 1;
    memcpy(buffer, start, length);
    buffer[length] = '\0';
    return 0;
}

static int handle_sync_session_summary(int fd, const struct protocol_request *request, const char *line, const struct app_config *config)
{
    struct board_test_item_summary items[BOARD_TEST_ITEM_CAPACITY];
    int item_count = 0;
    const char *cursor;
    char verdict[32];

    if (config == NULL) return send_failure(fd, request->session_id, 2200, "Board state config is unavailable");
    memset(items, 0, sizeof(items));
    if (json_get_string_value(line, "finalVerdict", verdict, sizeof(verdict)) != 0) {
        return send_failure(fd, request->session_id, 2201, "Missing finalVerdict");
    }

    cursor = strstr(line, "\"testResults\"");
    while (cursor != NULL && item_count < BOARD_TEST_ITEM_CAPACITY) {
        char test_id[40];
        char status[16];
        if (json_get_string_value(cursor, "testId", test_id, sizeof(test_id)) != 0) break;
        if (json_get_string_value(cursor, "status", status, sizeof(status)) != 0) break;
        snprintf(items[item_count].test_id, sizeof(items[item_count].test_id), "%s", test_id);
        snprintf(items[item_count].last_status, sizeof(items[item_count].last_status), "%s", status);
        items[item_count].test_count = 1;
        item_count++;
        cursor = strstr(cursor + 1, "\"testId\"");
    }

    if (board_state_update_last_result(config->board_state_path, request->session_id, "", "", verdict) != 0) {
        return send_failure(fd, request->session_id, 2202, "Unable to save session summary");
    }
    if (item_count > 0 && board_state_record_test_items(config->board_state_path, items, item_count) != 0) {
        return send_failure(fd, request->session_id, 2203, "Unable to save test item summary");
    }
    if (board_state_write_last_result_json(config->board_state_path, request->session_id, verdict, items, item_count) != 0) {
        return send_failure(fd, request->session_id, 2204, "Unable to save last result detail");
    }
    if (control_gen1_app_service("start") != 0) {
        return send_failure(fd, request->session_id, 2205, "Unable to restart gen1 application service");
    }
    return send_ok_response(fd, request->session_id, "Session summary synced");
}

static int send_board_state(int fd, const char *session_id, const struct app_config *config)
{
    struct board_state state;
    char data[4096];
    char line[4608];
    if (control_gen1_app_service("stop") != 0) {
        return send_failure(fd, session_id, 2206, "Unable to stop gen1 application service for testing");
    }
    if (config == NULL || board_state_load_from_file(config->board_state_path, &state) != 0) {
        board_state_load_defaults(&state);
    }
    board_state_to_json(&state, data, sizeof(data));
    protocol_build_response_envelope(line, sizeof(line), session_id, 0, "Board state loaded", data);
    return protocol_write_line(fd, line);
}

static int send_failure(int fd, const char *session_id, int code, const char *message)
{
    char line[1024];
    protocol_build_response_envelope(line, sizeof(line), session_id, code, message, "{}");
    return protocol_write_line(fd, line);
}

static int send_ok_response(int fd, const char *session_id, const char *message)
{
    char line[1024];
    protocol_build_response_envelope(line, sizeof(line), session_id, 0, message, "{}");
    return protocol_write_line(fd, line);
}

static int write_board_sn(int fd, const struct protocol_request *request)
{
    int rc = board_state_write_sn(request->sn);
    if (rc == 0) return send_ok_response(fd, request->session_id, "Board SN written");
    return send_failure(fd, request->session_id, 2100, "Unable to write board SN");
}

int session_manager_handle_client(int client_fd, const struct app_config *config)
{
    char line[PROTOCOL_MAX_LINE];
    struct protocol_request request;
    if (protocol_read_line(client_fd, line, sizeof(line)) <= 0) return -1;
    if (protocol_parse_request(line, &request) != 0) {
        return send_failure(client_fd, "", 1000, "Invalid protocol request");
    }
    if (strcmp(request.protocol_version, "1.0") != 0) {
        return send_failure(client_fd, request.session_id, 1001, "Unsupported protocol version");
    }
    if (strcmp(request.command_group, "sys") == 0 && strcmp(request.command, "get_board_state") == 0) {
        return send_board_state(client_fd, request.session_id, config);
    }
    if (strcmp(request.command_group, "sys") == 0 && strcmp(request.command, "get_md5") == 0) {
        return send_application_md5(client_fd, request.session_id, config);
    }
    if (strcmp(request.command_group, "sys") == 0 && strcmp(request.command, "get_version") == 0) {
        return send_application_version(client_fd, request.session_id, config);
    }
    if (strcmp(request.command_group, "sys") == 0 && strcmp(request.command, "write_sn") == 0) {
        return write_board_sn(client_fd, &request);
    }
    if (strcmp(request.command_group, "sys") == 0 && strcmp(request.command, "enter_test_mode") == 0) {
        return send_ok_response(client_fd, request.session_id, "Test mode entered");
    }
    if (strcmp(request.command_group, "sys") == 0 && strcmp(request.command, "sync_session_summary") == 0) {
        return handle_sync_session_summary(client_fd, &request, line, config);
    }
    if (strcmp(request.command_group, "session") == 0 && strcmp(request.command, "start") == 0)
        return test_runner_run_plan(client_fd, request.session_id, line, config);
    if (strcmp(request.command_group, "pressure") == 0 && strcmp(request.command, "start") == 0)
        return handle_pressure_start(client_fd, &request);
    if (strcmp(request.command_group, "pressure") == 0 && strcmp(request.command, "cpu") == 0)
        return handle_pressure_metric(client_fd, &request, 1);
    if (strcmp(request.command_group, "pressure") == 0 && strcmp(request.command, "memory") == 0)
        return handle_pressure_metric(client_fd, &request, 0);
    if (strcmp(request.command_group, "pressure") == 0 && strcmp(request.command, "load_start") == 0)
        return handle_pressure_load_start(client_fd, &request);
    if (strcmp(request.command_group, "pressure") == 0 && strcmp(request.command, "load_status") == 0)
        return send_pressure_load_status(client_fd, &request, "Persistent CPU/memory pressure status");
    if (strcmp(request.command_group, "pressure") == 0 && strcmp(request.command, "load_stop") == 0)
        return handle_pressure_load_stop(client_fd, &request);
    if (strcmp(request.command_group, "pressure") == 0 && strcmp(request.command, "wifi") == 0)
        return handle_pressure_wifi(client_fd, &request, config);
    if (strcmp(request.command_group, "pressure") == 0 && strcmp(request.command, "ethernet") == 0)
        return handle_pressure_ethernet(client_fd, &request, config);
    if (strcmp(request.command_group, "pressure") == 0 && strcmp(request.command, "bluetooth") == 0)
        return handle_pressure_bluetooth(client_fd, &request, config);
    if (strcmp(request.command_group, "pressure") == 0 && (strcmp(request.command, "fan") == 0 || strcmp(request.command, "hdmi") == 0))
        return handle_pressure_peripheral(client_fd, &request, request.command);
    if (strcmp(request.command_group, "pressure") == 0 && strcmp(request.command, "usb") == 0)
        return handle_pressure_peripheral(client_fd, &request, request.command);
    if (strcmp(request.command_group, "pressure") == 0 && strcmp(request.command, "camera") == 0)
        return handle_pressure_camera(client_fd, &request, config);
    if (strcmp(request.command_group, "pressure") == 0 && (strcmp(request.command, "emmc") == 0 || strcmp(request.command, "tf") == 0))
        return handle_pressure_storage(client_fd, &request, config, request.command);
    return send_failure(client_fd, request.session_id, 1002, "Unsupported command");
}
