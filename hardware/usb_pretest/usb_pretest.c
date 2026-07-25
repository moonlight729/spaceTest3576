#define _POSIX_C_SOURCE 200809L
#include "usb_pretest.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define USB_STEP_COUNT 8
struct usb_state {
    pthread_mutex_t lock;
    pthread_t thread;
    int running;
    int port;
    int step;
    int detected;
    int speed_mbps;
    int started;
    int completed;
    int failed;
    char mode[24];
    char message[160];
    char result_file[192];
};
static struct usb_state state = { .lock = PTHREAD_MUTEX_INITIALIZER, .port = 18080, .mode = "pcba" };

static const char *speed_name(int step) { return step < 4 ? "usb2" : "usb3"; }
static const char *port_name(int step) { return (step % 4) < 2 ? "port1" : "port2"; }
static const char *direction_name(int step) { return step % 2 == 0 ? "normal" : "reverse"; }

static int read_speed(const char *path, int *value)
{
    FILE *file = fopen(path, "r");
    int rc;
    if (file == NULL) return -1;
    rc = fscanf(file, "%d", value);
    fclose(file);
    return rc == 1 ? 0 : -1;
}

static int detect_usb(int *speed)
{
    DIR *dir = opendir("/sys/block");
    struct dirent *entry;
    int found = 0, best = 0;
    if (dir == NULL) return 0;
    while ((entry = readdir(dir)) != NULL) {
        char device_path[512];
        char resolved[512];
        char *cursor;
        int value = 0;
        if (entry->d_name[0] != 's' || entry->d_name[1] != 'd') continue;
        snprintf(device_path, sizeof(device_path), "/sys/block/%s/device", entry->d_name);
        if (realpath(device_path, resolved) == NULL) continue;
        cursor = resolved + strlen(resolved);
        while (cursor > resolved) {
            char speed_path[600];
            snprintf(speed_path, sizeof(speed_path), "%.*s/speed", (int)(cursor - resolved), resolved);
            if (read_speed(speed_path, &value) == 0 && value > 0) break;
            while (cursor > resolved && *cursor != '/') cursor--;
            while (cursor > resolved && *cursor == '/') cursor--;
        }
        if (value > 0) {
            found = 1;
            if (value > best) best = value;
        }
    }
    closedir(dir);
    if (speed != NULL) *speed = best;
    return found;
}

static void set_result_path(void)
{
    mkdir("/userdata/factory_test/usb", 0755);
    snprintf(state.result_file, sizeof(state.result_file), "/userdata/factory_test/usb/%s_usb_test.json",
             strcmp(state.mode, "finished_product") == 0 ? "finished_product" : "pcba");
}

static void create_desktop_shortcut(void)
{
    const char *path = "/home/originflow/Desktop/USB-Test.desktop";
    FILE *file;
    mkdir("/home/originflow/Desktop", 0755);
    file = fopen(path, "w");
    if (file == NULL) return;
    fprintf(file,
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=USB测试\n"
            "Comment=打开USB预检页面\n"
            "Exec=xdg-open http://127.0.0.1:%d/usb-test\n"
            "Icon=applications-internet\n"
            "Terminal=false\n"
            "Categories=Utility;\n",
            state.port);
    fclose(file);
    chmod(path, 0755);
    if (chown(path, 1002, 1002) != 0) {
        fprintf(stderr, "unable to set USB shortcut ownership: %s\n", strerror(errno));
    }
}

static void write_result(void)
{
    char temp[256];
    FILE *file;
    if (state.result_file[0] == '\0') return;
    snprintf(temp, sizeof(temp), "%s.tmp", state.result_file);
    file = fopen(temp, "w");
    if (file == NULL) return;
    fprintf(file, "{\"schemaVersion\":1,\"testMode\":\"%s\",\"overallResult\":\"%s\",\"step\":%d,\"usb2Cycles\":%d,\"usb3Cycles\":%d,\"speedMbps\":%d}\n",
            state.mode, state.completed && !state.failed ? "passed" : "failed", state.step,
            state.step > 4 ? 4 : state.step, state.step > 4 ? state.step - 4 : 0, state.speed_mbps);
    fflush(file);
    fsync(fileno(file));
    fclose(file);
    rename(temp, state.result_file);
}

static void reset_test(void)
{
    state.step = 0;
    state.detected = 0;
    state.speed_mbps = 0;
    state.started = 1;
    state.completed = 0;
    state.failed = 0;
    set_result_path();
    unlink(state.result_file);
    snprintf(state.message, sizeof(state.message), "Insert %s device into %s (%s)", speed_name(0), port_name(0), direction_name(0));
}

static void poll_usb(void)
{
    if (!state.started || state.completed || state.failed) return;
    state.detected = detect_usb(&state.speed_mbps);
    if (!state.detected)
        snprintf(state.message, sizeof(state.message), "Insert %s device into %s (%s)", speed_name(state.step), port_name(state.step), direction_name(state.step));
    else
        snprintf(state.message, sizeof(state.message), "USB detected at %d Mbps; confirm %s insertion", state.speed_mbps, direction_name(state.step));
}

static void state_json(char *buffer, size_t size)
{
    snprintf(buffer, size, "{\"mode\":\"%s\",\"step\":%d,\"totalSteps\":8,\"speed\":\"%s\",\"port\":\"%s\",\"direction\":\"%s\",\"detected\":%s,\"speedMbps\":%d,\"started\":%s,\"completed\":%s,\"failed\":%s,\"message\":\"%s\",\"resultFile\":\"%s\"}",
             state.mode, state.step + 1, speed_name(state.step), port_name(state.step), direction_name(state.step),
             state.detected ? "true" : "false", state.speed_mbps, state.started ? "true" : "false",
             state.completed ? "true" : "false", state.failed ? "true" : "false", state.message, state.result_file);
}

static void response(int fd, const char *type, const char *body)
{
    dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", type, strlen(body), body);
}

static void http_client(int fd)
{
    char request[4096], body[4096];
    ssize_t length = read(fd, request, sizeof(request) - 1);
    if (length <= 0) return;
    request[length] = '\0';
    pthread_mutex_lock(&state.lock);
    poll_usb();
    if (strncmp(request, "GET /usb-test", sizeof("GET /usb-test") - 1) == 0) {
        const char *html = "<!doctype html><html><meta charset='utf-8'><title>USB Test</title><style>body{font-family:sans-serif;background:#101828;color:white;padding:32px}button{font-size:22px;padding:12px;margin:8px}pre{font-size:22px}</style><h1>USB预检</h1><pre id=s>等待开始</pre><button onclick=start('pcba')>PCBA测试</button><button onclick=start('finished_product')>整机测试</button><button onclick=post('/api/usb/confirm-direction')>确认当前方向</button><button onclick=post('/api/usb/retry')>重试</button><button onclick=post('/api/usb/abort')>终止</button><script>async function post(u){await fetch(u,{method:'POST'});refresh()}async function start(m){await fetch('/api/usb/start',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:m})});refresh()}async function refresh(){s.textContent=JSON.stringify(await fetch('/api/usb/state').then(r=>r.json()),null,2)}setInterval(refresh,500);refresh()</script>";
        response(fd, "text/html; charset=utf-8", html);
    } else if (strncmp(request, "GET /api/usb/state", sizeof("GET /api/usb/state") - 1) == 0 || strncmp(request, "GET /api/usb/result", sizeof("GET /api/usb/result") - 1) == 0) {
        state_json(body, sizeof(body)); response(fd, "application/json", body);
    } else if (strncmp(request, "POST /api/usb/start", sizeof("POST /api/usb/start") - 1) == 0) {
        snprintf(state.mode, sizeof(state.mode), strstr(request, "finished_product") ? "finished_product" : "pcba"); reset_test(); state_json(body, sizeof(body)); response(fd, "application/json", body);
    } else if (strncmp(request, "POST /api/usb/retry", sizeof("POST /api/usb/retry") - 1) == 0) {
        state.failed = 0; state.detected = 0; state.message[0] = '\0'; poll_usb(); state_json(body, sizeof(body)); response(fd, "application/json", body);
    } else if (strncmp(request, "POST /api/usb/confirm-direction", sizeof("POST /api/usb/confirm-direction") - 1) == 0) {
        if (state.started && state.detected) {
            int speed_ok = state.step < 4 ? state.speed_mbps < 5000 : state.speed_mbps >= 5000;
            if (!speed_ok) {
                state.failed = 1;
                snprintf(state.message, sizeof(state.message), "Wrong USB speed %d Mbps for %s step; retry", state.speed_mbps, speed_name(state.step));
                write_result();
            } else {
                state.step++;
                state.detected = 0;
                if (state.step >= USB_STEP_COUNT) { state.completed = 1; snprintf(state.message, sizeof(state.message), "USB pretest completed"); }
                else snprintf(state.message, sizeof(state.message), "Insert %s device into %s (%s)", speed_name(state.step), port_name(state.step), direction_name(state.step));
                write_result();
            }
        }
        state_json(body, sizeof(body)); response(fd, "application/json", body);
    } else if (strncmp(request, "POST /api/usb/abort", sizeof("POST /api/usb/abort") - 1) == 0) {
        state.failed = 1; snprintf(state.message, sizeof(state.message), "USB pretest aborted"); write_result(); state_json(body, sizeof(body)); response(fd, "application/json", body);
    } else {
        dprintf(fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nNot Found");
    }
    pthread_mutex_unlock(&state.lock);
}

static void *worker(void *unused)
{
    int listener, one = 1;
    struct sockaddr_in address;
    (void)unused;
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return NULL;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET; address.sin_port = htons((uint16_t)state.port); inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listener, 4) != 0) { close(listener); return NULL; }
    for (;;) {
        fd_set readable;
        struct timeval timeout = { .tv_sec = 0, .tv_usec = 500000 };
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        if (select(listener + 1, &readable, NULL, NULL, &timeout) > 0 && FD_ISSET(listener, &readable)) {
            int client = accept(listener, NULL, NULL);
            if (client >= 0) { http_client(client); close(client); }
        }
        pthread_mutex_lock(&state.lock);
        poll_usb();
        pthread_mutex_unlock(&state.lock);
    }
}

int usb_pretest_start(const struct app_config *config)
{
    if (config != NULL && !config->usb_pretest_enabled) return 0;
    if (state.running) return 0;
    state.running = 1;
    if (config != NULL && config->usb_pretest_http_port > 0) state.port = config->usb_pretest_http_port;
    create_desktop_shortcut();
    return pthread_create(&state.thread, NULL, worker, NULL);
}
void usb_pretest_stop(void) { }
