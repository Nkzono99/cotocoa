#include "cotocoa_internal.h"
#include "cotocoa_plan.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct pending_frame {
    cotocoa_message *frame;
    unsigned char *seen;
    int64_t *offsets;
    int count;
    uint64_t volume;
    struct pending_frame *next;
} pending_frame;
struct cotocoa_assembler {
    int capacity, pending, bound;
    uint64_t max_bytes, elements, bytes;
    cotocoa_metadata schema;
    int64_t *last_steps;
    unsigned char *have_steps;
    pending_frame *frames;
};
static int invalid(cotocoa_error *e,const char *text)
{
    if(e) { e->code=COTOCOA_INVALID; snprintf(e->message,sizeof(e->message),"assembler: %s",text); }
    return COTOCOA_INVALID;
}
static void free_pending(pending_frame *p)
{
    if(p->frame) cotocoa_message_release(p->frame);
    free(p->seen); free(p->offsets); free(p);
}
int cotocoa_assembler_create(int capacity,uint64_t max_bytes,cotocoa_assembler **out,cotocoa_error *e)
{
    if(!out) return invalid(e,"missing output");
    *out=NULL;
    if(capacity<1 || capacity>1024 || !max_bytes || max_bytes>SIZE_MAX)
        return invalid(e,"require 1..1024 pending steps and a positive frame byte limit");
    cotocoa_assembler *a=calloc(1,sizeof(*a));
    if(!a) return invalid(e,"allocation failed");
    a->capacity=capacity; a->max_bytes=max_bytes; *out=a;
    return COTOCOA_OK;
}
static int schema_equal(const cotocoa_metadata *x,const cotocoa_metadata *y)
{
    return x->connection_id==y->connection_id && x->dtype==y->dtype && x->ndim==y->ndim &&
        x->source_count==y->source_count && x->mode==y->mode &&
        !memcmp(x->shape,y->shape,sizeof(x->shape)) &&
        !memcmp(x->global_shape,y->global_shape,sizeof(x->global_shape)) &&
        !memcmp(x->axes,y->axes,sizeof(x->axes)) && !memcmp(x->units,y->units,sizeof(x->units)) &&
        !memcmp(x->time_unit,y->time_unit,sizeof(x->time_unit)) &&
        !memcmp(x->name,y->name,sizeof(x->name)) && !memcmp(x->source,y->source,sizeof(x->source));
}
int cotocoa_assembler_push(cotocoa_assembler *a,const cotocoa_message *shard,cotocoa_message **out,cotocoa_error *e)
{
    if(!a || !shard || !out || !shard->data) return invalid(e,"missing assembler, shard or output");
    *out=NULL;
    const cotocoa_metadata *m=&shard->metadata;
    uint64_t elements=1, volume=1; size_t unit=ctco_itemsize(m->dtype);
    if(m->kind!=COTOCOA_STREAM || m->delivery!=COTOCOA_ALL || !unit || m->ndim<1 || m->ndim>3 ||
       m->source_count<1 || m->source_count>524288 || m->source_rank<0 || m->source_rank>=m->source_count ||
       (m->has_time && !isfinite(m->time))) return invalid(e,"requires a reliable stream with valid source metadata");
    for(int d=0;d<3;++d) {
        if(m->shape[d]<1 || m->global_shape[d]<m->shape[d] || m->offset[d]<0 ||
           m->offset[d]>m->global_shape[d]-m->shape[d] ||
           (d>=m->ndim && (m->shape[d]!=1 || m->global_shape[d]!=1 || m->offset[d]!=0)) ||
           (uint64_t)m->global_shape[d]>a->max_bytes/unit/elements)
            return invalid(e,"invalid shape/offset or frame exceeds max_frame_bytes");
        elements*=(uint64_t)m->global_shape[d]; volume*=(uint64_t)m->shape[d];
    }
    if(a->bound && !schema_equal(&a->schema,m)) return invalid(e,"connection/schema differs from the first shard");
    if(a->bound && a->have_steps[m->source_rank] && m->step<=a->last_steps[m->source_rank])
        return invalid(e,"duplicate or decreasing step from source rank");
    pending_frame **link=&a->frames;
    while(*link && (*link)->frame->metadata.step!=m->step) link=&(*link)->next;
    pending_frame *p=*link;
    if(p && (p->frame->metadata.has_time!=m->has_time || (m->has_time && p->frame->metadata.time!=m->time)))
        return invalid(e,"shards of a step have different times");
    if(p) for(int r=0;r<m->source_count;++r) if(p->seen[r]) {
        int overlap=1;
        for(int d=0;d<3;++d) {
            int64_t lo=p->offsets[r*3+d];
            if(m->offset[d]>=lo+m->shape[d] || lo>=m->offset[d]+m->shape[d]) overlap=0;
        }
        if(overlap) return invalid(e,"overlapping shards");
    }
    int count=p ? p->count : 0;
    uint64_t total=(p ? p->volume : 0)+volume;
    if(count+1==m->source_count && total!=elements) return invalid(e,"all sources arrived but global frame has holes");
    if(!p && a->pending==a->capacity) return invalid(e,"pending step capacity exceeded; finish older frames first");
    if(!a->bound) {
        a->last_steps=calloc((size_t)m->source_count,sizeof(int64_t));
        a->have_steps=calloc((size_t)m->source_count,1);
        if(!a->last_steps || !a->have_steps) {
            free(a->last_steps); free(a->have_steps); a->last_steps=NULL; a->have_steps=NULL;
            return invalid(e,"allocation failed for source tracking");
        }
        a->schema=*m; a->elements=elements; a->bytes=elements*unit; a->bound=1;
    }
    if(!p) {
        p=calloc(1,sizeof(*p));
        if(!p) return invalid(e,"allocation failed for pending frame");
        p->frame=calloc(1,sizeof(cotocoa_message));
        if(p->frame) {
            p->frame->completed=1;
            p->frame->data=malloc((size_t)a->bytes);
        }
        p->seen=calloc((size_t)m->source_count,1);
        p->offsets=calloc((size_t)m->source_count*3,sizeof(int64_t));
        if(!p->frame || !p->frame->data || !p->seen || !p->offsets) {
            free_pending(p); return invalid(e,"allocation failed for frame");
        }
        p->frame->metadata=*m;
        p->frame->metadata.source_rank=-1; p->frame->metadata.sequence=0;
        for(int d=0;d<3;++d) { p->frame->metadata.shape[d]=m->global_shape[d]; p->frame->metadata.offset[d]=0; }
        *link=p; ++a->pending;
    }
    size_t n=0;
    for(int64_t k=0;k<m->shape[2];++k) for(int64_t j=0;j<m->shape[1];++j) {
        uint64_t index=((uint64_t)(k+m->offset[2])*m->global_shape[1]+j+m->offset[1])*m->global_shape[0]+m->offset[0];
        size_t row=(size_t)m->shape[0]*unit;
        memcpy((char *)p->frame->data+index*unit,(const char *)shard->data+n,row); n+=row;
    }
    p->seen[m->source_rank]=1;
    memcpy(p->offsets+m->source_rank*3,m->offset,sizeof(m->offset));
    p->volume=total; ++p->count;
    a->have_steps[m->source_rank]=1; a->last_steps[m->source_rank]=m->step;
    if(p->count==m->source_count) {
        *out=p->frame; p->frame=NULL; *link=p->next; --a->pending; free_pending(p);
    }
    return COTOCOA_OK;
}
int cotocoa_assembler_finish(cotocoa_assembler *a,cotocoa_error *e)
{
    if(!a) return invalid(e,"missing assembler");
    if(a->pending) {
        if(e) {
            e->code=COTOCOA_STATE;
            snprintf(e->message,sizeof(e->message),"assembler: %d unfinished step(s); step %lld has %d/%d sources",
                a->pending,(long long)a->frames->frame->metadata.step,a->frames->count,a->schema.source_count);
        }
        return COTOCOA_STATE;
    }
    return COTOCOA_OK;
}
void cotocoa_assembler_release(cotocoa_assembler *a)
{
    if(!a) return;
    while(a->frames) { pending_frame *p=a->frames; a->frames=p->next; free_pending(p); }
    free(a->last_steps); free(a->have_steps); free(a);
}
