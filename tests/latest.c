#define _POSIX_C_SOURCE 200809L
#include "cotocoa.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static void require(int ok,cotocoa_context *ctx,const char *text)
{
    if(!ok) { cotocoa_error e={.code=-1}; snprintf(e.message,sizeof(e.message),"latest: %s",text); cotocoa_abort(ctx,&e); }
}
int main(int argc,char **argv)
{
    if(argc!=3) return 2;
    cotocoa_context *ctx=NULL; cotocoa_error error={0};
    if(cotocoa_open(argv[1],argv[2],&ctx,&error)) cotocoa_abort(ctx,&error);
    int rank; MPI_Comm_rank(cotocoa_comm(ctx),&rank);
    if(!strcmp(argv[1],"producer")) {
        double field[3]; int64_t shape[1]={3};
        cotocoa_array_view view=cotocoa_view_f64(field,1,shape,0);
        for(int step=1;step<=50;++step) {
            for(int i=0;i<3;++i) field[i]=step*1000+rank*100+i;
            cotocoa_send_options options=cotocoa_send_defaults(); options.step=step; options.offset[0]=rank*3;
            if(cotocoa_send(ctx,"field",&view,&options,&error)) cotocoa_abort(ctx,&error);
            memset(field,0,sizeof(field));
        }
    } else {
        int last[2]={0},count=0;
        for(;;) {
            cotocoa_message *m;
            int rc=cotocoa_receive(ctx,"field",&m,&error);
            if(rc==COTOCOA_END) break;
            if(rc) cotocoa_abort(ctx,&error);
            cotocoa_metadata meta; cotocoa_array_view view;
            if(cotocoa_message_metadata(m,&meta,&error)) cotocoa_abort(ctx,&error);
            if(cotocoa_message_view(m,&view,&error)) cotocoa_abort(ctx,&error);
            require(meta.source_rank>=0 && meta.source_rank<2 && meta.sequence==meta.step,ctx,"metadata");
            require(meta.step>last[meta.source_rank],ctx,"sequence must increase");
            for(int i=0;i<3;++i)
                require(((const double *)view.data)[i]==meta.step*1000+meta.source_rank*100+i,ctx,"snapshot was overwritten");
            if(!count) { struct timespec delay={0,200000000}; nanosleep(&delay,NULL); }
            last[meta.source_rank]=(int)meta.step; ++count;
            if(cotocoa_complete(m,&error)) cotocoa_abort(ctx,&error);
            cotocoa_message_release(m);
        }
        require(last[0]==50 && last[1]==50 && count<100,ctx,"latest must drop pending data and retain final frames");
        printf("PASS latest C rank=%d messages=%d final=%d,%d\n",rank,count,last[0],last[1]);
    }
    if(cotocoa_close(ctx,&error)) cotocoa_abort(NULL,&error);
    return 0;
}
