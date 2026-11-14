#include "cotocoa.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    cotocoa_context *ctx = NULL;
    cotocoa_error error = {0};
    if (argc != 2) return 2;
    if (cotocoa_open(argv[1], "cotocoa.yaml", &ctx, &error)) cotocoa_abort(ctx,&error);
    int rank;
    MPI_Comm_rank(cotocoa_comm(ctx), &rank);
    if (!strcmp(argv[1],"producer")) {
        double field[3][5][7];
        const int64_t shape[3] = {3,5,7};
        cotocoa_array_view view = cotocoa_view_f64(&field[0][0][0],3,shape,COTOCOA_ORDER_C);
        for (int step=1;step<=10;++step) {
            for(int i=0;i<3;++i) for(int j=0;j<5;++j) for(int k=0;k<7;++k)
                field[i][j][k]=step+rank;
            cotocoa_send_options options = cotocoa_send_defaults();
            options.step=step; options.offset[0]=rank*3;
            if(cotocoa_send(ctx,"potential",&view,&options,&error)) cotocoa_abort(ctx,&error);
        }
    } else {
        for (;;) {
            cotocoa_message *message;
            int rc=cotocoa_receive(ctx,"potential",&message,&error);
            if(rc==COTOCOA_END) break;
            if(rc!=COTOCOA_OK) cotocoa_abort(ctx,&error);
            cotocoa_metadata meta;
            cotocoa_array_view view;
            if(cotocoa_message_metadata(message,&meta,&error)) cotocoa_abort(ctx,&error);
            if(cotocoa_message_view(message,&view,&error)) cotocoa_abort(ctx,&error);
            printf("step=%lld source=%d first=%g\n",(long long)meta.step,meta.source_rank,
                   *(const double *)view.data);
            if(cotocoa_complete(message,&error)) cotocoa_abort(ctx,&error);
            cotocoa_message_release(message);
        }
    }
    if(cotocoa_close(ctx,&error)) cotocoa_abort(NULL,&error);
    return 0;
}
