/* Local contract tests use the same internal message representation as receive. */
#include "cotocoa_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static cotocoa_error error;
static cotocoa_message shard(int source,int step)
{
    static int32_t data[3]={1,2,3};
    cotocoa_message m={.data=data,.completed=1};
    cotocoa_metadata *x=&m.metadata;
    x->dtype=COTOCOA_INT32; x->ndim=1; x->source_count=2; x->source_rank=source; x->step=step;
    x->shape[0]=3; x->global_shape[0]=6; x->offset[0]=source*3;
    x->shape[1]=x->shape[2]=x->global_shape[1]=x->global_shape[2]=1;
    return m;
}
static cotocoa_assembler *create(int steps)
{
    cotocoa_assembler *a=NULL;
    assert(!cotocoa_assembler_create(steps,1024,&a,&error)); return a;
}
int main(void)
{
    cotocoa_assembler *a=create(2); cotocoa_message *out=NULL;
    cotocoa_message x=shard(0,1),y=shard(1,1);
    assert(!cotocoa_assembler_push(a,&x,&out,&error) && !out);
    assert(cotocoa_assembler_finish(a,&error)<0 && strstr(error.message,"1/2"));
    assert(cotocoa_assembler_push(a,&x,&out,&error)<0 && strstr(error.message,"duplicate"));
    y.metadata.has_time=1;
    assert(cotocoa_assembler_push(a,&y,&out,&error)<0 && strstr(error.message,"times"));
    y.metadata.has_time=0; y.metadata.offset[0]=2;
    assert(cotocoa_assembler_push(a,&y,&out,&error)<0 && strstr(error.message,"overlapping"));
    y.metadata.offset[0]=3;
    assert(!cotocoa_assembler_push(a,&y,&out,&error) && out);
    cotocoa_array_view v; assert(!cotocoa_message_view(out,&v,&error));
    assert(v.shape[0]==6 && ((const int32_t *)v.data)[5]==3);
    cotocoa_message_release(out); out=NULL;
    assert(!cotocoa_assembler_finish(a,&error));
    assert(cotocoa_assembler_push(a,&y,&out,&error)<0);
    x.metadata.step=2; assert(!cotocoa_assembler_push(a,&x,&out,&error));
    x.metadata.step=3; assert(!cotocoa_assembler_push(a,&x,&out,&error));
    x.metadata.step=4;
    assert(cotocoa_assembler_push(a,&x,&out,&error)<0 && strstr(error.message,"capacity"));
    cotocoa_assembler_release(a);
    a=create(1); x=shard(0,1); y=shard(1,1);
    x.metadata.global_shape[0]=y.metadata.global_shape[0]=7;
    assert(!cotocoa_assembler_push(a,&x,&out,&error));
    assert(cotocoa_assembler_push(a,&y,&out,&error)<0 && strstr(error.message,"holes"));
    cotocoa_assembler_release(a);
    a=create(1); x=shard(0,1); x.metadata.delivery=COTOCOA_LATEST;
    assert(cotocoa_assembler_push(a,&x,&out,&error)<0);
    x=shard(0,1); x.metadata.global_shape[0]=INT64_MAX;
    assert(cotocoa_assembler_push(a,&x,&out,&error)<0 && strstr(error.message,"max_frame_bytes"));
    cotocoa_assembler_release(a);
    /* Eight tiles exercise offsets in all three axes and out-of-order sources. */
    a=create(2);
    const int order[8]={7,0,5,2,3,6,1,4};
    float tile[24];
    for(int n=0;n<8;++n) {
        int r=order[n],ox=(r%2)*2,oy=((r/2)%2)*3,oz=(r/4)*4;
        cotocoa_message m={.data=tile,.completed=1};
        cotocoa_metadata *meta=&m.metadata;
        meta->dtype=COTOCOA_FLOAT32; meta->ndim=3; meta->source_count=8; meta->source_rank=r;
        meta->step=5; meta->has_time=1; meta->time=0.25;
        for(int d=0;d<3;++d) { meta->shape[d]=d+2; meta->global_shape[d]=2*(d+2); }
        meta->offset[0]=ox; meta->offset[1]=oy; meta->offset[2]=oz;
        for(int k=0;k<4;++k) for(int j=0;j<3;++j) for(int i=0;i<2;++i)
            tile[(k*3+j)*2+i]=(float)(1000*(k+oz)+100*(j+oy)+i+ox);
        assert(!cotocoa_assembler_push(a,&m,&out,&error));
        assert((out!=NULL)==(n==7));
    }
    assert(!cotocoa_message_view(out,&v,&error));
    for(int k=0;k<8;++k) for(int j=0;j<6;++j) for(int i=0;i<4;++i)
        assert(((const float *)v.data)[(k*6+j)*4+i]==1000*k+100*j+i);
    cotocoa_message_release(out);
    assert(!cotocoa_assembler_finish(a,&error)); cotocoa_assembler_release(a);
    puts("PASS bounded assembly, duplicates, times, overlap, holes, capacity, missing sources, size limits and 3D tiles");
}
