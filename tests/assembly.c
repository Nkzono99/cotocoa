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
    puts("PASS bounded assembly, duplicates, times, overlap, holes, capacity, missing sources and size limits");
}
