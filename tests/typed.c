#include "cotocoa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int condition, cotocoa_context *ctx, const char *text)
{
    if (!condition) {
        cotocoa_error error = {.code = -1};
        snprintf(error.message, sizeof(error.message), "typed array test: %s", text);
        cotocoa_abort(ctx, &error);
    }
}

static double value(const char *pointer, int dtype)
{
    double d; float f; int32_t i;
    if (dtype == COTOCOA_FLOAT64) { memcpy(&d,pointer,8); return d; }
    if (dtype == COTOCOA_FLOAT32) { memcpy(&f,pointer,4); return f; }
    memcpy(&i,pointer,4); return i;
}

static void store(char *pointer, int dtype, double v)
{
    float f = (float)v; int32_t i = (int32_t)v;
    if (dtype == COTOCOA_FLOAT64) memcpy(pointer,&v,8);
    else if (dtype == COTOCOA_FLOAT32) memcpy(pointer,&f,4);
    else memcpy(pointer,&i,4);
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    int dtype = atoi(getenv("COTOCOA_TEST_DTYPE"));
    int ndim = atoi(getenv("COTOCOA_TEST_NDIM"));
    int64_t shape[3] = {3, ndim>=2 ? 5 : 1, ndim>=3 ? 7 : 1};
    cotocoa_context *ctx = NULL;
    cotocoa_error error = {0};
    if (cotocoa_open(argv[1],argv[2],&ctx,&error)) cotocoa_abort(ctx,&error);
    int rank; MPI_Comm_rank(cotocoa_comm(ctx),&rank);
    if (!strcmp(argv[1],"producer")) {
        char storage[6*5*7*8];
        for (int step=1;step<=8;++step) {
            cotocoa_array_view view = dtype==1 ? cotocoa_view_f64((double *)storage,ndim,shape,0) :
                (dtype==2 ? cotocoa_view_f32((float *)storage,ndim,shape,0) :
                            cotocoa_view_i32((int32_t *)storage,ndim,shape,0));
            view.strides[0] *= 2;
            char *base = storage;
            if (step%2) { base += 2*view.strides[0]; view.strides[0] *= -1; }
            view.data = base;
            for(int k=0;k<shape[2];++k) for(int j=0;j<shape[1];++j) for(int i=0;i<3;++i)
                store(base+i*view.strides[0]+j*view.strides[1]+k*view.strides[2],dtype,
                      -1000000+rank*1000000+step*1000+i*100+j*10+k);
            cotocoa_send_options options = cotocoa_send_defaults();
            options.step=step; options.offset[0]=rank*3;
            if(cotocoa_send(ctx,"field",&view,&options,&error)) cotocoa_abort(ctx,&error);
            memset(storage,0,sizeof(storage));
        }
    } else {
        int count = 0;
        for (;;) {
            cotocoa_message *message;
            int rc=cotocoa_receive(ctx,"field",&message,&error);
            if (rc==COTOCOA_END) break;
            if (rc) cotocoa_abort(ctx,&error);
            cotocoa_metadata m; cotocoa_array_view view;
            if(cotocoa_message_metadata(message,&m,&error)) cotocoa_abort(ctx,&error);
            if(cotocoa_message_view(message,&view,&error)) cotocoa_abort(ctx,&error);
            require(m.dtype==dtype && m.ndim==ndim && view.dtype==dtype && view.ndim==ndim,ctx,"dtype/rank");
            require(m.offset[0]==m.source_rank*3 && m.sequence==m.step,ctx,"metadata");
            for(int d=0;d<3;++d) require(view.shape[d]==shape[d],ctx,"shape");
            for(int k=0;k<shape[2];++k) for(int j=0;j<shape[1];++j) for(int i=0;i<3;++i)
                require(value((const char *)view.data+i*view.strides[0]+j*view.strides[1]+k*view.strides[2],dtype)
                    == -1000000+m.source_rank*1000000+m.step*1000+i*100+j*10+k,ctx,"signed value/stride");
            if(cotocoa_complete(message,&error)) cotocoa_abort(ctx,&error);
            cotocoa_message_release(message);
            ++count;
        }
        require(count==16,ctx,"delivery count");
        printf("PASS typed C dtype=%d ndim=%d rank=%d\n",dtype,ndim,rank);
    }
    if(cotocoa_close(ctx,&error)) cotocoa_abort(NULL,&error);
    return 0;
}
