#include "cotocoa.h"
#include <stdio.h>
#include <string.h>

int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    cotocoa_context *ctx=NULL; cotocoa_error e={0};
    if(cotocoa_open(argv[1],"tasks.yaml",&ctx,&e)) cotocoa_abort(ctx,&e);
    if(!strcmp(argv[1],"producer")) {
        cotocoa_ticket *tickets[10];
        for(int step=0;step<10;++step) {
            double input[3]={step,step+1,step+2}; int64_t shape[1]={3};
            cotocoa_array_view v=cotocoa_view_f64(input,1,shape,COTOCOA_ORDER_C);
            cotocoa_send_options o=cotocoa_send_defaults(); o.step=step;
            if(cotocoa_submit(ctx,"sum",&v,&o,&tickets[step],&e)) cotocoa_abort(ctx,&e);
        }
        for(int i=0;i<10;++i) {
            if(cotocoa_ticket_wait(tickets[i],&e)) cotocoa_abort(ctx,&e);
            cotocoa_ticket_release(tickets[i]);
        }
    } else for(;;) {
        cotocoa_message *task=NULL;
        int rc=cotocoa_next_task(ctx,"sum",&task,&e);
        if(rc==COTOCOA_END) break;
        if(rc) cotocoa_abort(ctx,&e);
        cotocoa_array_view v; cotocoa_metadata m;
        if(cotocoa_message_view(task,&v,&e) || cotocoa_message_metadata(task,&m,&e)) cotocoa_abort(ctx,&e);
        const double *x=v.data;
        printf("step=%lld sum=%g\n",(long long)m.step,x[0]+x[1]+x[2]);
        if(cotocoa_complete(task,&e)) cotocoa_abort(ctx,&e);
        cotocoa_message_release(task);
    }
    if(cotocoa_close(ctx,&e)) cotocoa_abort(NULL,&e);
    puts("PASS task example C"); return 0;
}
