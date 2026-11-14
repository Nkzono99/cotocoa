#ifndef COTOCOA_PLAN_H
#define COTOCOA_PLAN_H
#include "cotocoa.h"
#include <stddef.h>

#define COTOCOA_MAX_PROGRAMS 64
#define COTOCOA_MAX_CONNECTIONS 256
enum { CTCO_PRODUCER = 0, CTCO_COUPLER = 1, CTCO_CONSUMER = 2 };
enum { CTCO_STREAM = 0, CTCO_TASK = 1, CTCO_ALL = 0, CTCO_LATEST = 1 };
enum { CTCO_SNAPSHOT = 0, CTCO_BORROW = 1 };

typedef struct {
    char name[64];
    int role, ranks, group_size, world_offset, source_offset;
} ctco_program;

typedef struct {
    char name[64], source_port[64], target_port[64];
    char axes[3][64], units[64], time_unit[64];
    int producer, consumer, dtype, ndim, slots, kind, delivery, mode;
    int64_t shape[3], global_shape[3];
    size_t elements, bytes;
} ctco_connection;

typedef struct {
    char digest[65];
    uint64_t file_hash;
    double timeout;
    int program_count, connection_count, source_count, world_size, coupler_rank;
    int max_requests, queue_capacity;
    ctco_program programs[COTOCOA_MAX_PROGRAMS];
    ctco_connection connections[COTOCOA_MAX_CONNECTIONS];
} ctco_plan;

size_t ctco_itemsize(int dtype);
int ctco_plan_read(const char *path, ctco_plan *plan, cotocoa_error *error);
#endif
