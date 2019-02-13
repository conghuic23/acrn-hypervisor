/*
 * Project Acrn
 * Acrn-dm-trace-tool
 *
 */

#include <string.h>
#include <pthread.h>
#include "trace.h"

#define TRACE_INIT_FAIL -1
#define TRACE_INIT_SUCCESS 0

#define LOG_PATH "/tmp/acrn-dm.log"
#define PIPE_LOG "/tmp/acrn-pipe-log"

struct debug_buffer *db;
static pthread_mutex_t db_mtx = PTHREAD_MUTEX_INITIALIZER;

void dm_debug(const char *format,...)
{
    char buffer[MAX_BUFFLEN + 1];
    char msg[MAX_MSGLEN];
    va_list args;
    int ret;
    va_start(args, format);

    // convert input format to string
    ret = vsnprintf(msg, MAX_MSGLEN, format, args);
    if (ret < 0) {
        printf("dm_debug error!\n");
        return;
    }

    // joint rdtsc()
    ret = snprintf(buffer, MAX_BUFFLEN, "%lu: %s", rdtsc(), msg);
    if (ret < 0) {
        printf("dm_debug error!\n");
        return;
    }

    // add to ring buffer and output to pipe
    debug_buffer_add((void *)&buffer);

    //printf("%s", buffer);
}

int debug_buffer_init(size_t max_size)
{
    if (max_size < MAX_BUFFLEN) return TRACE_INIT_FAIL;

    db = malloc(sizeof(struct debug_buffer));
    db->depth =  (max_size / MAX_BUFFLEN) - 1;
    db->width = MAX_BUFFLEN;
    db->ptr = db->last_write_ptr = -1;
    db->buffer = calloc(db->depth, MAX_BUFFLEN);
    db->fd = -1;

    if (db->buffer == NULL) return TRACE_INIT_FAIL;

    /* unlink(PIPE_LOG);
    ret = mkfifo(PIPE_LOG, 0777);
    if (ret == -1) return TRACE_INIT_FAIL;
    */
    printf("trace debug init success\n [buffer length] %u, [trace size] %lu, [trace depth] %lu\n",
            MAX_BUFFLEN, max_size, db->depth);
    return TRACE_INIT_SUCCESS;
}

void debug_buffer_close()
{
    if (db->buffer) free(db->buffer);
    db->buffer = NULL;

    if (db->fd > 0) {
        close(db->fd);
    }

    printf("trace debug close success\n");
}

void debug_buffer_add(void *msg)
{
    size_t ptr;

    /* use atomic operate to ensure thread safety */
    //ptr = atomic_add_fetch(&db->ptr, 1) % db->depth;

    pthread_mutex_lock(&db_mtx);

    ptr = (++db->ptr) % db->depth;

    if (db->ptr >= db->depth) {
        db->ptr %= db->depth;
        db->reversed = 1;
    }
    // printf("ptr: %lu, %s\r", ptr, (char *)msg);

    memcpy(db->buffer + ptr * db->width, msg, db->width);

    pthread_mutex_unlock(&db_mtx);
    //debug_pipe_write(msg);

    // atomic_store(&db->ptr, db->ptr + 1);
    // db->ptr++;
}

void debug_pipe_write(const void *buff)
{
    int ret;
    if (db->fd < 0) {
        db->fd = open(PIPE_LOG, O_WRONLY);
    }
    ret = write(db->fd, buff, MAX_BUFFLEN);
    if (ret == -1) printf("pipe write error!\n");
}

int debug_flush()
{
    int ret = 0;
    size_t wptr = 0;
    char buff[MAX_BUFFLEN + 1];

    // No content
    pthread_mutex_lock(&db_mtx);
    if (db->ptr < 0) return ret;

    FILE *fp = fopen(LOG_PATH, "w");
    if (fp == NULL) return ret;

    if (db->reversed) {
        wptr = (atomic_load(&db->ptr) + 1) % db->depth;
    } else {
        wptr = 0;
    }

    while (wptr != db->ptr) {
        memcpy(buff, db->buffer + wptr * db->width, db->width);

        ret = fprintf(fp, "%s", buff);
        if (ret < 0) {
            printf("fprintf error!\n");
            return ret;
        }
        wptr++;
        if (wptr >= db->depth) {
            wptr %= db->depth;
        }
    }

    fclose(fp);
    pthread_mutex_unlock(&db_mtx);
    return 1;
}

int debug_clear()
{
    /*
    atomic_store(&db->ptr, -1);
    atomic_store(&db->reversed, 0);
    */
    FILE *fp;
    pthread_mutex_lock(&db_mtx);

    db->ptr = -1;
    db->reversed = 0;

    /* clear log file */
    fp = fopen(LOG_PATH, "w");
    fclose(fp);

    pthread_mutex_unlock(&db_mtx);

    return 1;
}

int
vm_parse_tracesize(const char *optarg, size_t *ret_tracesize)
{
    char *endptr;
    size_t optval;
    int shift;

    optval = strtoul(optarg, &endptr, 0);
    switch (tolower((unsigned char)*endptr)) {
        case 'm':
            shift = 20;
            break;
        case 'k':
            shift = 10;
            break;
        case 'b':
        case '\0':
            shift = 0;
        default:
            return -1;
    }

    optval = optval << shift;
    if (optval < 10 * MB / 1024UL)
        return -1;

    *ret_tracesize = optval;

    return 0;
}

struct dbuffer_wrapper vm_monitor_trace(void *arg)
{
    struct dbuffer_wrapper dbuffer;

    dbuffer.depth = db->depth;
    dbuffer.width = db->width;

    pthread_mutex_lock(&db_mtx);
    dbuffer.ptr = db->ptr;
    dbuffer.reversed = db->reversed;
    pthread_mutex_unlock(&db_mtx);

    dbuffer.buffer = db->buffer;
    return dbuffer;
}

int vm_monitor_flush(void *arg)
{
    return debug_flush();
}

int vm_monitor_clear(void *arg)
{
    return debug_clear();
}
