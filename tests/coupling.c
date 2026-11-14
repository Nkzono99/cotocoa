#define _POSIX_C_SOURCE 200809L
#include "cotocoa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void require(int condition, cotocoa_context *ctx, const char *text)
{
    if (!condition) {
        cotocoa_error error = {.code = -1};
        snprintf(error.message, sizeof(error.message), "test failed: %s", text);
        cotocoa_abort(ctx, &error);
    }
}

static void check(int rc, cotocoa_context *ctx, cotocoa_error *error)
{
    if (rc < 0) cotocoa_abort(ctx, error);
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    int borrowed = getenv("COTOCOA_TEST_BORROWED") != NULL;
    if (borrowed) MPI_Init(&argc, &argv);
    cotocoa_context *ctx;
    cotocoa_error error = {0};
    check(cotocoa_open(argv[1], argv[2], &ctx, &error), NULL, &error);
    int rank;
    MPI_Comm_rank(cotocoa_comm(ctx), &rank);
    int steps = getenv("COTOCOA_TEST_STEPS") ? atoi(getenv("COTOCOA_TEST_STEPS")) : 100;
    const int64_t shape[3] = {3, 5, 7};
    if (!strcmp(argv[1], "producer")) {
        double field[3][5][7];
        cotocoa_array_view view = cotocoa_view_f64(&field[0][0][0], 3, shape, COTOCOA_ORDER_C);
        for (int step = 1; step <= steps; ++step) {
            for (int i=0;i<3;++i) for (int j=0;j<5;++j) for (int k=0;k<7;++k)
                field[i][j][k] = rank*1000000 + step*1000 + i*100 + j*10 + k;
            cotocoa_send_options options = cotocoa_send_defaults();
            options.step = step; options.has_time = 1; options.time = step*0.01;
            options.offset[0] = rank*3;
            check(cotocoa_send(ctx, "field", &view, &options, &error), ctx, &error);
            memset(field, 0, sizeof(field));
        }
        check(cotocoa_flush(ctx, &error), ctx, &error);
    } else {
        int count = 0, source_ranks;
        MPI_Comm_size(MPI_COMM_WORLD, &source_ranks);
        int consumer_ranks;
        MPI_Comm_size(cotocoa_comm(ctx), &consumer_ranks);
        source_ranks -= consumer_ranks+1;
        cotocoa_message *saved = NULL;
        for (;;) {
            cotocoa_message *message;
            int rc = cotocoa_receive(ctx, "field", &message, &error);
            if (rc == COTOCOA_END) break;
            check(rc, ctx, &error);
            cotocoa_array_view view;
            cotocoa_metadata m;
            check(cotocoa_message_view(message, &view, &error), ctx, &error);
            check(cotocoa_message_metadata(message, &m, &error), ctx, &error);
            require(m.offset[0] == m.source_rank*3 && m.sequence == m.step, ctx, "metadata");
            require(m.has_time && m.time == m.step*0.01, ctx, "time metadata");
            require(m.shape[0]==3 && m.shape[1]==5 && m.shape[2]==7 &&
                    m.global_shape[0]==source_ranks*3, ctx, "shape metadata");
            require(!strcmp(m.axes[0],"x") && !strcmp(m.units,"V"), ctx, "units/axes");
            for (int i=0;i<3;++i) for (int j=0;j<5;++j) for (int k=0;k<7;++k) {
                double value;
                memcpy(&value, (const char *)view.data + i*view.strides[0] + j*view.strides[1] + k*view.strides[2], 8);
                require(value == m.source_rank*1000000 + m.step*1000 + i*100+j*10+k, ctx, "snapshot/axis order");
            }
            if (count == 0) {
                struct timespec delay = {0, 10000000};
                nanosleep(&delay, NULL);
            }
            check(cotocoa_complete(message, &error), ctx, &error);
            if (!saved) saved = message;
            else cotocoa_message_release(message);
            ++count;
        }
        require(count == steps*source_ranks, ctx, "all delivery count");
        if (saved) {
            cotocoa_array_view view;
            check(cotocoa_message_view(saved, &view, &error), ctx, &error);
            require(*(const double *)view.data >= 1000, ctx, "retained snapshot");
            cotocoa_message_release(saved);
        }
        printf("PASS C rank=%d messages=%d\n", rank, count);
    }
    check(cotocoa_close(ctx, &error), NULL, &error);
    int finalized;
    MPI_Finalized(&finalized);
    require(finalized == !borrowed, NULL, "MPI ownership");
    if (borrowed) {
        MPI_Barrier(MPI_COMM_SELF);
        MPI_Finalize();
    }
    return 0;
}
