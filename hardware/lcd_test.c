#define _POSIX_C_SOURCE 200809L
#include "lcd_test.h"
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static int g_running, g_stop, g_fd = -1;
static void *g_map;
static size_t g_size, g_stride;
static int g_w, g_h;
static const uint16_t colors[] = {0xF800,0x07E0,0x001F,0xFFE0,0xF81F,0x07FF,0xFFFF,0x0000};
static void draw(int vertical) { uint16_t *p=(uint16_t*)g_map; int bands=8; if (!vertical) { int bh=g_h/bands; if(!bh)bh=1; for(int y=0;y<g_h;y++) for(int x=0;x<g_w;x++) p[y*(g_stride/2)+x]=colors[(y/bh<bands)?y/bh:7]; } else { int bw=g_w/bands; if(!bw)bw=1; for(int y=0;y<g_h;y++) for(int x=0;x<g_w;x++) p[y*(g_stride/2)+x]=colors[(x/bw<bands)?x/bw:7]; } }
static void *lcd_thread(void *arg) { (void)arg; while (!g_stop) { draw(0); sleep(1); if(g_stop)break; draw(1); sleep(1); } return NULL; }
int lcd_test_start(const char *path) { struct fb_var_screeninfo v; pthread_mutex_lock(&g_lock); if(g_running){pthread_mutex_unlock(&g_lock);return 0;} g_fd=open(path?path:"/dev/fb0",O_RDWR); if(g_fd<0||ioctl(g_fd,FBIOGET_VSCREENINFO,&v)<0||v.bits_per_pixel!=16){if(g_fd>=0)close(g_fd);g_fd=-1;pthread_mutex_unlock(&g_lock);return -1;} g_w=v.xres;g_h=v.yres;g_stride=v.xres_virtual*2;g_size=g_stride*v.yres_virtual;g_map=mmap(NULL,g_size,PROT_READ|PROT_WRITE,MAP_SHARED,g_fd,0);if(g_map==MAP_FAILED){close(g_fd);g_fd=-1;pthread_mutex_unlock(&g_lock);return -1;} g_stop=0; if(pthread_create(&g_thread,NULL,lcd_thread,NULL)!=0){munmap(g_map,g_size);close(g_fd);g_fd=-1;pthread_mutex_unlock(&g_lock);return -1;} g_running=1; pthread_mutex_unlock(&g_lock); return 0; }
void lcd_test_stop(void){pthread_mutex_lock(&g_lock);if(!g_running){pthread_mutex_unlock(&g_lock);return;}g_stop=1;pthread_t t=g_thread;pthread_mutex_unlock(&g_lock);pthread_join(t,NULL);pthread_mutex_lock(&g_lock);munmap(g_map,g_size);close(g_fd);g_fd=-1;g_running=0;pthread_mutex_unlock(&g_lock);}
