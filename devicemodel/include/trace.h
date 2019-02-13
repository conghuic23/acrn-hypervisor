#ifndef _CORE_TRACE_
#define _CORE_TRACE_

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <atomic.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define KB    (1024UL)
#define MB    (1024 * 1024UL)

#define MAX_PREVLEN 23
#define MAX_MSGLEN 222
#define BUFF_PADING 11
#define MAX_BUFFLEN (MAX_PREVLEN + MAX_MSGLEN + BUFF_PADING)
#define FLUSH_FREQ 10

#define DM_PRINT(...) do { \
    u_int64_t start = rdtsc(); \
    printf(__VA_ARGS__); \
    u_int64_t end = rdtsc(); \
    printf("cycles: %lu\n", end - start); \
} while(0)

#define DM_DEBUG(...) do { \
    char buffer[MAX_BUFFLEN + 1];         \
    char msg[MAX_MSGLEN];                \
    snprintf(msg, MAX_MSGLEN, __VA_ARGS__);     \
    snprintf(buffer, MAX_BUFFLEN, "[%8lu]%s: %s", rdtsc() , __func__, msg); \
    debug_buffer_add((void *)&buffer);        \
} while(0)

struct debug_buffer {
    size_t depth;
    size_t width;
    size_t ptr;

    void *buffer;

    size_t last_write_ptr;
    unsigned reversed;
    int fd;
};

/*
 * struct to pass data to dm monitor
 */
struct dbuffer_wrapper {
    size_t depth;
    size_t width;
    size_t ptr;
    unsigned reversed;
    void *buffer;
};

static inline u_int64_t rdtsc(void)
{
    u_int32_t lo, hi;

    asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
    return ((u_int64_t)hi << 32) | lo;
}

void dm_debug(const char *format, ...);
int debug_buffer_init(size_t max_size);
void debug_buffer_close(void);

void debug_buffer_add(void *msg);

void debug_pipe_write();
int debug_flush(void);

int vm_parse_tracesize(const char *optarg, size_t *ret_tracesize);

struct dbuffer_wrapper vm_monitor_trace(void *arg);
int vm_monitor_flush(void *arg);
int vm_monitor_clear(void *arg);

#endif
