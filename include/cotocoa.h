#ifndef COTOCOA_H
#define COTOCOA_H

#include <stdint.h>
#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { COTOCOA_OK = 0, COTOCOA_END = 1,
       COTOCOA_INVALID = -1, COTOCOA_STATE = -2, COTOCOA_MEMORY = -3 };
enum { COTOCOA_FLOAT64 = 1, COTOCOA_FLOAT32 = 2, COTOCOA_INT32 = 3,
       COTOCOA_ORDER_C = 0, COTOCOA_ORDER_F = 1 };
enum { COTOCOA_STREAM = 0, COTOCOA_TASK = 1 };
enum { COTOCOA_ALL = 0, COTOCOA_LATEST = 1,
       COTOCOA_SNAPSHOT = 0, COTOCOA_BORROW = 1 };

typedef struct cotocoa_context cotocoa_context;
typedef struct cotocoa_message cotocoa_message;
typedef struct cotocoa_ticket cotocoa_ticket;
typedef struct cotocoa_assembler cotocoa_assembler;

typedef struct {
    int code;
    char message[512];
} cotocoa_error;

typedef struct {
    const void *data;
    int dtype, ndim;
    int64_t shape[3], strides[3];
} cotocoa_array_view;

typedef struct {
    int64_t step;
    int has_time;
    double time;
    int64_t offset[3];
} cotocoa_send_options;

typedef struct {
    int64_t step, sequence;
    int source_rank, has_time;
    double time;
    int64_t shape[3], global_shape[3], offset[3];
    char axes[3][64], units[64], time_unit[64];
    int dtype, ndim;
    int kind, connection_id, source_count;
    char name[64], port[64], source[64];
    int delivery, mode;
} cotocoa_metadata;

/* open/close are collective across the entire coupled job. config is a native
 * manifest, or the source YAML when COTOCOA_MANIFEST names its compiled manifest.
 * One context per process, for one open/close lifetime. */
int cotocoa_open(const char *name, const char *config,
                 cotocoa_context **out, cotocoa_error *error);
int cotocoa_close(cotocoa_context *ctx, cotocoa_error *error);
MPI_Comm cotocoa_comm(const cotocoa_context *ctx);
MPI_Fint cotocoa_comm_f(const cotocoa_context *ctx);
void cotocoa_abort(cotocoa_context *ctx, const cotocoa_error *error);

cotocoa_array_view cotocoa_view_f64(const double *data, int ndim,
                                   const int64_t shape[3], int order);
cotocoa_array_view cotocoa_view_f32(const float *data, int ndim,
                                   const int64_t *shape, int order);
cotocoa_array_view cotocoa_view_i32(const int32_t *data, int ndim,
                                   const int64_t *shape, int order);
cotocoa_send_options cotocoa_send_defaults(void);
int cotocoa_send(cotocoa_context *ctx, const char *name,
                 const cotocoa_array_view *array,
                 const cotocoa_send_options *options, cotocoa_error *error);
int cotocoa_flush(cotocoa_context *ctx, cotocoa_error *error);
int cotocoa_submit(cotocoa_context *ctx, const char *operation,
                   const cotocoa_array_view *array,
                   const cotocoa_send_options *options,
                   cotocoa_ticket **out, cotocoa_error *error);
/* Borrowed input must remain allocated and unchanged until the ticket retires.
 * release(ticket) does not release that input: wait/test or Context.flush does. */
int cotocoa_borrow(cotocoa_context *ctx, const char *name,
                   const cotocoa_array_view *array,
                   const cotocoa_send_options *options,
                   cotocoa_ticket **out, cotocoa_error *error);
int cotocoa_submit_borrow(cotocoa_context *ctx, const char *operation,
                          const cotocoa_array_view *array,
                          const cotocoa_send_options *options,
                          cotocoa_ticket **out, cotocoa_error *error);
int cotocoa_ticket_test(cotocoa_ticket *ticket, int *done, cotocoa_error *error);
int cotocoa_ticket_wait(cotocoa_ticket *ticket, cotocoa_error *error);
void cotocoa_ticket_retain(cotocoa_ticket *ticket);
void cotocoa_ticket_release(cotocoa_ticket *ticket);

/* receive and complete are collective within the consumer group. Successful
 * receive transfers ownership of a local snapshot to the caller. Complete
 * acknowledges processing; release frees the local message, after complete. */
int cotocoa_receive(cotocoa_context *ctx, const char *name,
                    cotocoa_message **out, cotocoa_error *error);
/* NULL or "*" receives any incoming port. Named receive requires all incoming
 * connections to use that port; it never silently skips queued messages. */
int cotocoa_next_task(cotocoa_context *ctx, const char *operation,
                      cotocoa_message **out, cotocoa_error *error);
int cotocoa_message_view(const cotocoa_message *message,
                         cotocoa_array_view *out, cotocoa_error *error);
int cotocoa_message_metadata(const cotocoa_message *message,
                             cotocoa_metadata *out, cotocoa_error *error);
int cotocoa_complete(cotocoa_message *message, cotocoa_error *error);
void cotocoa_message_release(cotocoa_message *message);

/* Local assembly of one reliable stream connection. push copies a shard and
 * never acknowledges it; the caller still completes the input message.
 * A ready frame is already complete: release it after use, without complete().
 * Source steps must strictly increase. Pending frames and their bytes are bounded.
 * finish reports missing shards; release discards local storage without MPI. */
int cotocoa_assembler_create(int max_pending_steps, uint64_t max_frame_bytes,
                            cotocoa_assembler **out, cotocoa_error *error);
int cotocoa_assembler_push(cotocoa_assembler *assembler, const cotocoa_message *shard,
                          cotocoa_message **frame, cotocoa_error *error);
int cotocoa_assembler_finish(cotocoa_assembler *assembler, cotocoa_error *error);
void cotocoa_assembler_release(cotocoa_assembler *assembler);

/* For the supplied native Coupler executable. */
int cotocoa_couple(cotocoa_context *ctx, cotocoa_error *error);

#ifdef __cplusplus
}
#endif
#endif
