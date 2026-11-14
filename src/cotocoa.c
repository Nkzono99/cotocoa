#include "cotocoa.h"
#include "ctca.h"
#include "ctca_internal.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_WORDS 48
#define MAGIC 0x43544341
#define PRODUCER 0
#define COUPLER 1
#define CONSUMER 2

_Static_assert(sizeof(int) == 4, "CoToCoA requires 32-bit C int");
_Static_assert(sizeof(double) == 8, "CoToCoA requires 64-bit double");

typedef struct {
    char digest[65], producer[64], consumer[64], field[64];
    char source_port[64], target_port[64];
    char axes[3][64], units[64], time_unit[64];
    int producer_ranks, consumer_ranks, slots;
    int64_t shape[3], global_shape[3];
    uint64_t file_hash;
} manifest;

struct cotocoa_context {
    manifest plan;
    int role, rank, owns_mpi, area, ended, next_slot;
    size_t elements;
    int64_t sequence, *handles, *received_sequences;
    double *buffer;
    cotocoa_message *active;
};

struct cotocoa_message {
    cotocoa_context *context;
    cotocoa_metadata metadata;
    double *data;
    int completed;
};

static int context_used;

static int fail(cotocoa_error *error, int code, const char *format, ...)
{
    if (error) {
        va_list args;
        error->code = code;
        va_start(args, format);
        vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
    }
    return code;
}

void cotocoa_abort(cotocoa_context *ctx, const cotocoa_error *error)
{
    int initialized = 0, finalized = 0, rank = -1;
    MPI_Initialized(&initialized);
    MPI_Finalized(&finalized);
    if (initialized && !finalized) MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    fprintf(stderr, "cotocoa: rank=%d field=%s: %s\n", rank,
            ctx ? ctx->plan.field : "startup",
            error ? error->message : "unrecoverable error");
    fflush(stderr);
    if (initialized && !finalized) MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    exit(EXIT_FAILURE);
}

static void collective_check(cotocoa_context *ctx, int local_error,
                             cotocoa_error *error, MPI_Comm comm)
{
    int any_error;
    MPI_Allreduce(&local_error, &any_error, 1, MPI_INT, MPI_MAX, comm);
    if (any_error) {
        if (!local_error)
            fail(error, COTOCOA_INVALID, "another rank failed collective validation");
        cotocoa_abort(ctx, error);
    }
}

static int token(const char *text)
{
    size_t n = strlen(text);
    if (!n || n >= 64) return 0;
    for (size_t i = 0; i < n; ++i)
        if (!((text[i] >= 'a' && text[i] <= 'z') ||
              (text[i] >= 'A' && text[i] <= 'Z') ||
              (text[i] >= '0' && text[i] <= '9') ||
              text[i] == '_' || text[i] == '-')) return 0;
    return 1;
}

static int line(FILE *file, char *out, size_t capacity)
{
    char buffer[256];
    if (!fgets(buffer, sizeof(buffer), file)) return 0;
    size_t n = strlen(buffer);
    if (!n || buffer[n-1] != '\n' || n > capacity) return 0;
    buffer[n-1] = '\0';
    memcpy(out, buffer, n);
    return 1;
}

static int read_manifest(const char *path, manifest *p, cotocoa_error *error)
{
    FILE *file = path ? fopen(path, "rb") : NULL;
    char text[256], extra;
    int ok = 0, ch;
    if (!file) return fail(error, COTOCOA_INVALID, "cannot open manifest '%s': %s; run cotocoa plan --output first",
                            path ? path : "", strerror(errno));
    p->file_hash = UINT64_C(14695981039346656037);
    size_t length = 0;
    while ((ch = fgetc(file)) != EOF) {
        p->file_hash = (p->file_hash ^ (unsigned char)ch) * UINT64_C(1099511628211);
        if (++length > 8192) goto done;
    }
    rewind(file);
    if (!line(file, text, sizeof(text)) || strcmp(text, "COTOCOA_MANIFEST 1")) goto done;
    if (!line(file, text, sizeof(text)) || strlen(text) != 64) goto done;
    memcpy(p->digest, text, sizeof(p->digest));
    for (int i = 0; i < 64; ++i)
        if (!((p->digest[i] >= '0' && p->digest[i] <= '9') ||
              (p->digest[i] >= 'a' && p->digest[i] <= 'f'))) goto done;
    if (!line(file, text, sizeof(text)) ||
        sscanf(text, "%63s %d %c", p->producer, &p->producer_ranks, &extra) != 2) goto done;
    if (!line(file, text, sizeof(text)) ||
        sscanf(text, "%63s %d %c", p->consumer, &p->consumer_ranks, &extra) != 2) goto done;
    if (!line(file, p->field, sizeof(p->field)) ||
        !line(file, p->source_port, sizeof(p->source_port)) ||
        !line(file, p->target_port, sizeof(p->target_port))) goto done;
    if (!line(file, text, sizeof(text)) ||
        sscanf(text, "%" SCNd64 " %" SCNd64 " %" SCNd64 " %c",
               &p->shape[0], &p->shape[1], &p->shape[2], &extra) != 3) goto done;
    if (!line(file, text, sizeof(text)) ||
        sscanf(text, "%" SCNd64 " %" SCNd64 " %" SCNd64 " %c",
               &p->global_shape[0], &p->global_shape[1], &p->global_shape[2], &extra) != 3) goto done;
    if (!line(file, text, sizeof(text)) || sscanf(text, "%d %c", &p->slots, &extra) != 1) goto done;
    for (int i = 0; i < 3; ++i)
        if (!line(file, p->axes[i], sizeof(p->axes[i])) || !token(p->axes[i])) goto done;
    if (!line(file, p->units, sizeof(p->units)) ||
        !line(file, p->time_unit, sizeof(p->time_unit)) || fgetc(file) != EOF) goto done;
    if (!token(p->producer) || !token(p->consumer) || !token(p->field) ||
        !token(p->source_port) || !token(p->target_port) ||
        !strcmp(p->producer, p->consumer) || !strcmp(p->producer, "__coupler__") ||
        !strcmp(p->consumer, "__coupler__")) goto done;
    if (p->producer_ranks < 1 || p->consumer_ranks < 1 ||
        p->producer_ranks > 1000000 || p->consumer_ranks > 1000000 ||
        p->slots < 1 || p->slots >= 4095 ||
        p->producer_ranks > INT_MAX/p->slots) goto done;
    size_t elements = 1;
    for (int i = 0; i < 3; ++i) {
        if (p->shape[i] < 1 || p->global_shape[i] < p->shape[i] ||
            p->shape[i] > INT_MAX/8 || elements > (size_t)(INT_MAX/8)/p->shape[i]) goto done;
        elements *= (size_t)p->shape[i];
    }
    if (elements > SIZE_MAX/sizeof(double)/(size_t)p->slots) goto done;
    ok = 1;
done:
    fclose(file);
    return ok ? COTOCOA_OK : fail(error, COTOCOA_INVALID, "invalid or unsupported manifest '%s'", path);
}

static void put64(int *header, int index, uint64_t value)
{
    uint32_t words[2] = {(uint32_t)value, (uint32_t)(value >> 32)};
    memcpy(header + index, words, sizeof(words));
}

static uint64_t get64(const int *header, int index)
{
    uint32_t words[2];
    memcpy(words, header + index, sizeof(words));
    return words[0] | ((uint64_t)words[1] << 32);
}

static void digest_words(const char *digest, int *out)
{
    unsigned char bytes[32];
    for (int i = 0; i < 32; ++i) {
        unsigned int byte;
        sscanf(digest + i*2, "%2x", &byte);
        bytes[i] = (unsigned char)byte;
    }
    memcpy(out, bytes, sizeof(bytes));
}

int cotocoa_open(const char *name, const char *config,
                 cotocoa_context **out, cotocoa_error *error)
{
    int initialized, finalized, world_rank, world_size;
    cotocoa_error fallback = {0};
    if (!error) error = &fallback;
    if (!out || !name || !config)
        return fail(error, COTOCOA_INVALID, "open requires a name, config and output pointer");
    *out = NULL;
    MPI_Finalized(&finalized);
    if (finalized || context_used)
        return fail(error, COTOCOA_STATE, "only one Context lifetime is supported per process");
    MPI_Initialized(&initialized);
    if (!initialized) MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    cotocoa_context *ctx = calloc(1, sizeof(*ctx));
    int bad = !ctx;
    if (bad) fail(error, COTOCOA_MEMORY, "cannot allocate Context");
    collective_check(ctx, bad, error, MPI_COMM_WORLD);
    ctx->owns_mpi = !initialized;
    const char *path = getenv("COTOCOA_MANIFEST");
    if (!path || !*path) path = config;
    bad = read_manifest(path, &ctx->plan, error) != COTOCOA_OK;
    collective_check(ctx, bad, error, MPI_COMM_WORLD);
    manifest *p = &ctx->plan;
    uint64_t low, high;
    MPI_Allreduce(&p->file_hash, &low, 1, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);
    MPI_Allreduce(&p->file_hash, &high, 1, MPI_UINT64_T, MPI_MAX, MPI_COMM_WORLD);
    int expected = p->producer_ranks + p->consumer_ranks + 1;
    const char *expected_name = world_rank < p->producer_ranks ? p->producer :
        (world_rank == p->producer_ranks ? "__coupler__" : p->consumer);
    bad = low != high || world_size != expected || strcmp(name, expected_name);
    if (bad) fail(error, COTOCOA_INVALID,
                  "startup mismatch: program=%s expected=%s ranks=%d expected_ranks=%d or manifest differs",
                  name, expected_name, world_size, expected);
    collective_check(ctx, bad, error, MPI_COMM_WORLD);
    /* The legacy transport sends native bytes; reject mixed layouts up front. */
    uint32_t endian = 1;
    int abi[5] = {(int)sizeof(int), (int)sizeof(double), (int)sizeof(size_t),
                  *(unsigned char *)&endian, (int)sizeof(MPI_Fint)};
    int abi_low[5], abi_high[5];
    MPI_Allreduce(abi, abi_low, 5, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    MPI_Allreduce(abi, abi_high, 5, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    bad = memcmp(abi_low, abi_high, sizeof(abi_low)) || sizeof(size_t) != 8 ||
          sizeof(MPI_Fint) != sizeof(int);
    if (bad) fail(error, COTOCOA_INVALID, "incompatible native MPI/data ABI across ranks");
    collective_check(ctx, bad, error, MPI_COMM_WORLD);
    ctx->role = world_rank < p->producer_ranks ? PRODUCER :
        (world_rank == p->producer_ranks ? COUPLER : CONSUMER);
    ctx->elements = (size_t)p->shape[0]*(size_t)p->shape[1]*(size_t)p->shape[2];
    if (ctx->role == PRODUCER) {
        ctx->buffer = calloc(ctx->elements*(size_t)p->slots, sizeof(double));
        ctx->handles = calloc((size_t)p->slots, sizeof(int64_t));
        bad = !ctx->buffer || !ctx->handles;
    } else if (ctx->role == CONSUMER) {
        ctx->received_sequences = calloc((size_t)p->producer_ranks, sizeof(int64_t));
        bad = !ctx->received_sequences;
    } else bad = 0;
    if (bad) fail(error, COTOCOA_MEMORY, "cannot allocate bounded communication buffers");
    collective_check(ctx, bad, error, MPI_COMM_WORLD);
    context_used = 1;
    if (ctx->role == PRODUCER)
        CTCAR_init_detail(1, p->slots, HEADER_WORDS);
    else if (ctx->role == COUPLER)
        CTCAC_init_detail(1, p->producer_ranks*p->slots, HEADER_WORDS, 1, 1);
    else
        CTCAW_init_detail(1, p->consumer_ranks, 1, HEADER_WORDS);
    MPI_Comm_rank(CTCA_subcomm, &ctx->rank);
    if (ctx->role == PRODUCER)
        CTCAR_regarea_real8(ctx->buffer, ctx->elements*(size_t)p->slots, &ctx->area);
    else if (ctx->role == COUPLER) CTCAC_regarea_real8(&ctx->area);
    else CTCAW_regarea_real8(&ctx->area);
    *out = ctx;
    return COTOCOA_OK;
}

MPI_Comm cotocoa_comm(const cotocoa_context *ctx)
{
    return ctx ? CTCA_subcomm : MPI_COMM_NULL;
}

MPI_Fint cotocoa_comm_f(const cotocoa_context *ctx)
{
    return MPI_Comm_c2f(cotocoa_comm(ctx));
}

cotocoa_array_view cotocoa_view_f64(const double *data, int ndim,
                                   const int64_t shape[3], int order)
{
    cotocoa_array_view view = {0};
    view.data = data;
    view.dtype = COTOCOA_FLOAT64;
    view.ndim = ndim;
    if (!shape || ndim != 3 || (order != COTOCOA_ORDER_C && order != COTOCOA_ORDER_F)) return view;
    memcpy(view.shape, shape, sizeof(view.shape));
    int64_t stride = sizeof(double);
    for (int d = 0; d < 3; ++d) {
        int i = order == COTOCOA_ORDER_F ? d : 2-d;
        if (shape[i] < 1 || shape[i] > INT64_MAX/stride) {
            memset(view.strides, 0, sizeof(view.strides));
            return view;
        }
        view.strides[i] = stride;
        stride *= shape[i];
    }
    return view;
}

cotocoa_send_options cotocoa_send_defaults(void)
{
    cotocoa_send_options options = {0};
    return options;
}

int cotocoa_send(cotocoa_context *ctx, const char *name,
                 const cotocoa_array_view *array,
                 const cotocoa_send_options *options, cotocoa_error *error)
{
    if (!ctx || ctx->role != PRODUCER || !name || !array || !array->data)
        return fail(error, COTOCOA_INVALID, "send requires a producer Context and array");
    if (strcmp(name, ctx->plan.source_port) || array->dtype != COTOCOA_FLOAT64 || array->ndim != 3)
        return fail(error, COTOCOA_INVALID, "send: unknown port, dtype or dimensions");
    cotocoa_send_options defaults = cotocoa_send_defaults();
    if (!options) options = &defaults;
    if (ctx->sequence == INT64_MAX || (options->has_time && !isfinite(options->time)))
        return fail(error, COTOCOA_INVALID, "send: invalid sequence or non-finite time");
    int64_t span = 0;
    for (int d = 0; d < 3; ++d) {
        if (array->shape[d] != ctx->plan.shape[d] || array->strides[d] < 8 ||
            options->offset[d] < 0 || options->offset[d] > ctx->plan.global_shape[d]-array->shape[d] ||
            array->strides[d] > (PTRDIFF_MAX-span)/(array->shape[d]))
            return fail(error, COTOCOA_INVALID, "send: shape, stride or global offset mismatch at axis %d", d);
        span += (array->shape[d]-1)*array->strides[d];
    }
    int slot = ctx->next_slot;
    if (ctx->handles[slot]) CTCAR_wait(ctx->handles[slot]);
    double *dest = ctx->buffer + (size_t)slot*ctx->elements;
    const char *base = array->data;
    size_t n = 0;
    for (int64_t k = 0; k < array->shape[2]; ++k)
        for (int64_t j = 0; j < array->shape[1]; ++j)
            for (int64_t i = 0; i < array->shape[0]; ++i)
                memcpy(dest + n++, base + i*array->strides[0] + j*array->strides[1] + k*array->strides[2], 8);
    ctca_sync_area(ctx->area);
    int header[HEADER_WORDS] = {0};
    header[0] = MAGIC; header[1] = 1; header[2] = 1;
    header[3] = slot; header[4] = ctx->rank; header[5] = !!options->has_time;
    put64(header, 6, (uint64_t)++ctx->sequence);
    put64(header, 8, (uint64_t)options->step);
    uint64_t time_bits;
    memcpy(&time_bits, &options->time, sizeof(time_bits));
    put64(header, 10, time_bits);
    for (int d = 0; d < 3; ++d) {
        put64(header, 12 + d*2, (uint64_t)options->offset[d]);
        put64(header, 18 + d*2, (uint64_t)array->shape[d]);
    }
    put64(header, 24, ctx->elements);
    digest_words(ctx->plan.digest, header + 26);
    CTCAR_sendreq_hdl(header, HEADER_WORDS, &ctx->handles[slot]);
    ctx->next_slot = (slot+1)%ctx->plan.slots;
    return COTOCOA_OK;
}

int cotocoa_flush(cotocoa_context *ctx, cotocoa_error *error)
{
    if (!ctx || ctx->role != PRODUCER)
        return fail(error, COTOCOA_STATE, "flush requires a producer Context");
    for (int i = 0; i < ctx->plan.slots; ++i) {
        if (ctx->handles[i]) CTCAR_wait(ctx->handles[i]);
        ctx->handles[i] = 0;
    }
    return COTOCOA_OK;
}

static int validate_header(cotocoa_context *ctx, const int *h, int source,
                            cotocoa_error *error)
{
    int digest[8];
    digest_words(ctx->plan.digest, digest);
    if (h[0] != MAGIC || h[1] != 1 || h[2] != 1 || h[3] < 0 ||
        h[3] >= ctx->plan.slots || source < 0 || source >= ctx->plan.producer_ranks ||
        h[4] != source || (h[5] != 0 && h[5] != 1) ||
        get64(h, 6) < 1 || get64(h, 6) > INT64_MAX ||
        h[3] != (int)((get64(h, 6)-1) % (uint64_t)ctx->plan.slots) ||
        get64(h, 24) != ctx->elements || memcmp(h+26, digest, sizeof(digest)))
        return fail(error, COTOCOA_INVALID, "invalid protocol header from source rank %d", source);
    for (int d = 0; d < 3; ++d)
        if (get64(h, 18+d*2) != (uint64_t)ctx->plan.shape[d] ||
            get64(h, 12+d*2) > (uint64_t)(ctx->plan.global_shape[d]-ctx->plan.shape[d]))
            return fail(error, COTOCOA_INVALID, "invalid shape/offset in header from source rank %d", source);
    return COTOCOA_OK;
}

int cotocoa_receive(cotocoa_context *ctx, const char *name,
                    cotocoa_message **out, cotocoa_error *error)
{
    cotocoa_error fallback = {0};
    if (!error) error = &fallback;
    if (!ctx || ctx->role != CONSUMER)
        return fail(error, COTOCOA_STATE, "receive requires a consumer Context");
    int bad = !name || !out || strcmp(name, ctx->plan.target_port) || ctx->active;
    if (bad) fail(error, COTOCOA_STATE, "receive: wrong port or previous message is incomplete");
    collective_check(ctx, bad, error, CTCA_subcomm);
    *out = NULL;
    if (ctx->ended) return COTOCOA_END;
    int header[HEADER_WORDS], source;
    CTCAW_pollreq(&source, header, HEADER_WORDS);
    if (CTCAW_isfin()) {
        ctx->ended = 1;
        return COTOCOA_END;
    }
    bad = validate_header(ctx, header, source, error) != COTOCOA_OK;
    collective_check(ctx, bad, error, CTCA_subcomm);
    int64_t sequence = (int64_t)get64(header, 6);
    bad = ctx->received_sequences[source] == INT64_MAX ||
          sequence != ctx->received_sequences[source] + 1;
    if (bad) fail(error, COTOCOA_INVALID, "out-of-order snapshot from source rank %d", source);
    collective_check(ctx, bad, error, CTCA_subcomm);
    cotocoa_message *message = calloc(1, sizeof(*message));
    if (message) message->data = malloc(ctx->elements*sizeof(double));
    bad = !message || !message->data;
    if (bad) fail(error, COTOCOA_MEMORY, "cannot allocate received snapshot");
    collective_check(ctx, bad, error, CTCA_subcomm);
    if (ctx->rank == 0)
        CTCAW_readarea_real8(ctx->area, source, (size_t)header[3]*ctx->elements,
                            ctx->elements, message->data);
    MPI_Bcast(message->data, (int)ctx->elements, MPI_DOUBLE, 0, CTCA_subcomm);
    message->context = ctx;
    cotocoa_metadata *m = &message->metadata;
    m->step = (int64_t)get64(header, 8);
    m->sequence = sequence; m->source_rank = source; m->has_time = header[5];
    uint64_t time_bits = get64(header, 10);
    memcpy(&m->time, &time_bits, sizeof(m->time));
    memcpy(m->shape, ctx->plan.shape, sizeof(m->shape));
    memcpy(m->global_shape, ctx->plan.global_shape, sizeof(m->global_shape));
    memcpy(m->axes, ctx->plan.axes, sizeof(m->axes));
    memcpy(m->units, ctx->plan.units, sizeof(m->units));
    memcpy(m->time_unit, ctx->plan.time_unit, sizeof(m->time_unit));
    for (int d = 0; d < 3; ++d) m->offset[d] = (int64_t)get64(header, 12+d*2);
    ctx->received_sequences[source] = sequence;
    ctx->active = message;
    *out = message;
    return COTOCOA_OK;
}

int cotocoa_message_view(const cotocoa_message *message,
                         cotocoa_array_view *out, cotocoa_error *error)
{
    if (!message || !out) return fail(error, COTOCOA_INVALID, "message_view requires a message and output");
    *out = cotocoa_view_f64(message->data, 3, message->metadata.shape, COTOCOA_ORDER_F);
    return COTOCOA_OK;
}

int cotocoa_message_metadata(const cotocoa_message *message,
                             cotocoa_metadata *out, cotocoa_error *error)
{
    if (!message || !out) return fail(error, COTOCOA_INVALID, "message_metadata requires a message and output");
    *out = message->metadata;
    return COTOCOA_OK;
}

int cotocoa_complete(cotocoa_message *message, cotocoa_error *error)
{
    cotocoa_error fallback = {0};
    if (!error) error = &fallback;
    if (!message) return fail(error, COTOCOA_INVALID, "complete requires a message");
    if (message->completed) return fail(error, COTOCOA_STATE, "message already completed");
    CTCAW_complete();
    message->context->active = NULL;
    message->context = NULL;
    message->completed = 1;
    return COTOCOA_OK;
}

void cotocoa_message_release(cotocoa_message *message)
{
    if (!message) return;
    if (!message->completed) {
        cotocoa_error error;
        fail(&error, COTOCOA_STATE, "cannot release an incomplete message");
        cotocoa_abort(message->context, &error);
    }
    free(message->data);
    free(message);
}

int cotocoa_couple(cotocoa_context *ctx, cotocoa_error *error)
{
    cotocoa_error fallback = {0};
    if (!error) error = &fallback;
    if (!ctx || ctx->role != COUPLER)
        return fail(error, COTOCOA_STATE, "couple requires the standard Coupler Context");
    while (!ctx->ended) {
        int reqinfo[CTCAC_REQINFOITEMS], source, header[HEADER_WORDS];
        CTCAC_pollreq(reqinfo, &source, header, HEADER_WORDS);
        if (CTCAC_isfin()) { ctx->ended = 1; break; }
        if (validate_header(ctx, header, source, error) != COTOCOA_OK)
            cotocoa_abort(ctx, error);
        CTCAC_enqreq(reqinfo, 1, header, HEADER_WORDS);
    }
    return COTOCOA_OK;
}

int cotocoa_close(cotocoa_context *ctx, cotocoa_error *error)
{
    cotocoa_error fallback = {0};
    if (!error) error = &fallback;
    if (!ctx) return fail(error, COTOCOA_STATE, "close requires an open Context");
    if (ctx->role != PRODUCER && (!ctx->ended || ctx->active)) {
        fail(error, COTOCOA_STATE, "consumer/Coupler must receive the end of stream before close");
        cotocoa_abort(ctx, error);
    }
    if (ctx->role == PRODUCER) {
        cotocoa_flush(ctx, error);
        CTCAR_finalize();
    } else if (ctx->role == COUPLER) CTCAC_finalize();
    else CTCAW_finalize();
    int owns_mpi = ctx->owns_mpi;
    free(ctx->buffer);
    free(ctx->handles);
    free(ctx->received_sequences);
    free(ctx);
    if (owns_mpi) MPI_Finalize();
    return COTOCOA_OK;
}
