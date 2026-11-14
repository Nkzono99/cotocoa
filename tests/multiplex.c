#define _POSIX_C_SOURCE 200809L
#include "cotocoa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void require(int ok,cotocoa_context *ctx,const char *text)
{
    if(!ok) { cotocoa_error e={.code=-1}; snprintf(e.message,sizeof(e.message),"multiplex: %s",text); cotocoa_abort(ctx,&e); }
}
static void store(char *p,int type,double value)
{
    float f=(float)value; int32_t i=(int32_t)value;
    if(type==1) memcpy(p,&value,8); else if(type==2) memcpy(p,&f,4); else memcpy(p,&i,4);
}
static double read_value(const char *p,int type)
{
    double d; float f; int32_t i;
    if(type==1) { memcpy(&d,p,8); return d; }
    if(type==2) { memcpy(&f,p,4); return f; }
    memcpy(&i,p,4); return i;
}
int main(int argc,char **argv)
{
    if(argc!=3) return 2;
    int task=getenv("COTOCOA_TEST_TASK")!=NULL;
    cotocoa_context *ctx=NULL; cotocoa_error error={0};
    if(cotocoa_open(argv[1],argv[2],&ctx,&error)) cotocoa_abort(ctx,&error);
    int rank,world; MPI_Comm_rank(cotocoa_comm(ctx),&rank); MPI_Comm_rank(MPI_COMM_WORLD,&world);
    if(!strncmp(argv[1],"producer",8)) {
        cotocoa_ticket *tickets[72]={0}; int count=0;
        int base=!strcmp(argv[1],"producer2") ? 2000000 : 1000000;
        for(int step=1;step<=8;++step) for(int type=1;type<=3;++type) for(int ndim=1;ndim<=3;++ndim) {
            char storage[3*5*7*8],port[64]; int64_t shape[3]={3,ndim>=2?5:1,ndim>=3?7:1};
            snprintf(port,sizeof(port),"field_%d_%d",type,ndim);
            cotocoa_array_view view=type==1 ? cotocoa_view_f64((double *)storage,ndim,shape,0) :
                (type==2 ? cotocoa_view_f32((float *)storage,ndim,shape,0) : cotocoa_view_i32((int32_t *)storage,ndim,shape,0));
            for(int k=0;k<shape[2];++k) for(int j=0;j<shape[1];++j) for(int i=0;i<3;++i)
                store(storage+i*view.strides[0]+j*view.strides[1]+k*view.strides[2],type,
                      base+rank*100000+step*1000+i*100+j*10+k);
            cotocoa_send_options options=cotocoa_send_defaults(); options.step=step; options.offset[0]=rank*3;
            if(task) {
                if(cotocoa_submit(ctx,port,&view,&options,&tickets[count],&error)) cotocoa_abort(ctx,&error);
                int done;
                if(cotocoa_ticket_test(tickets[count],&done,&error)) cotocoa_abort(ctx,&error);
                if(!count) { cotocoa_ticket_release(tickets[count]); tickets[count]=NULL; }
                ++count;
            } else if(cotocoa_send(ctx,port,&view,&options,&error)) cotocoa_abort(ctx,&error);
            memset(storage,0,sizeof(storage));
        }
        if(cotocoa_flush(ctx,&error)) cotocoa_abort(ctx,&error);
        for(int i=0;i<count;++i) if(tickets[i]) {
            if(cotocoa_ticket_wait(tickets[i],&error)) cotocoa_abort(ctx,&error);
        }
        if(cotocoa_close(ctx,&error)) cotocoa_abort(NULL,&error);
        for(int i=0;i<count;++i) if(tickets[i]) {
            int done=0;
            if(cotocoa_ticket_test(tickets[i],&done,&error)) cotocoa_abort(NULL,&error);
            require(done,NULL,"ticket must survive close");
            cotocoa_ticket_release(tickets[i]);
        }
    } else {
        int count=0;
        for(;;) {
            cotocoa_message *m;
            int rc=task ? cotocoa_next_task(ctx,NULL,&m,&error) : cotocoa_receive(ctx,NULL,&m,&error);
            if(rc==COTOCOA_END) break;
            if(rc) cotocoa_abort(ctx,&error);
            cotocoa_metadata meta; cotocoa_array_view view;
            if(cotocoa_message_metadata(m,&meta,&error)) cotocoa_abort(ctx,&error);
            if(cotocoa_message_view(m,&view,&error)) cotocoa_abort(ctx,&error);
            require(meta.kind==task && meta.sequence==meta.step,ctx,"kind/sequence");
            int base=!strcmp(meta.source,"producer2") ? 2000000 : 1000000;
            char port[64]; snprintf(port,sizeof(port),"field_%d_%d",meta.dtype,meta.ndim);
            require(!strcmp(meta.port,port) && meta.offset[0]==meta.source_rank*3,ctx,"port/offset");
            require(meta.global_shape[0]==meta.source_count*3,ctx,"source metadata");
            for(int k=0;k<meta.shape[2];++k) for(int j=0;j<meta.shape[1];++j) for(int i=0;i<3;++i)
                require(read_value((const char *)view.data+i*view.strides[0]+j*view.strides[1]+k*view.strides[2],meta.dtype)
                    ==base+meta.source_rank*100000+meta.step*1000+i*100+j*10+k,ctx,"values");
            if(!count && task) { struct timespec delay={0,20000000}; nanosleep(&delay,NULL); }
            if(cotocoa_complete(m,&error)) cotocoa_abort(ctx,&error);
            if(!rank) printf("DONE %s %s %d %d %d %lld %d\n",argv[1],meta.source,meta.source_rank,meta.dtype,
                             meta.ndim,(long long)meta.step,world);
            cotocoa_message_release(m); ++count;
        }
        printf("PASS multiplex C rank=%d messages=%d\n",world,count);
        if(cotocoa_close(ctx,&error)) cotocoa_abort(NULL,&error);
    }
    return 0;
}
