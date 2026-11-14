#include "cotocoa.h"
#include "cotocoa_plan.h"
#include "ctca.h"
#include "ctca_internal.h"
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_WORDS 48
#define MAGIC 0x43544341
_Static_assert(sizeof(int)==4 && sizeof(float)==4 && sizeof(double)==8,
               "CoToCoA requires 32-bit int/float and 64-bit double");

typedef struct {
    char *buffer;
    int area,next_slot;
    int64_t sequence,*handles,*received_sequences;
} route_state;
struct cotocoa_context {
    ctco_plan plan;
    route_state *routes;
    int role,program,rank,source_rank,owns_mpi,ended;
    MPI_Comm comm;
    cotocoa_message *active;
    cotocoa_ticket *tickets;
};
struct cotocoa_message {
    cotocoa_context *context;
    cotocoa_metadata metadata;
    void *data;
    int completed;
};
struct cotocoa_ticket {
    cotocoa_context *context;
    cotocoa_ticket *next;
    int64_t handle;
    int completed;
    unsigned int references;
};
static int context_used;

static int fail(cotocoa_error *error,int code,const char *format,...)
{
    if(error) {
        va_list args;
        error->code=code;
        va_start(args,format);
        vsnprintf(error->message,sizeof(error->message),format,args);
        va_end(args);
    }
    return code;
}
void cotocoa_abort(cotocoa_context *ctx,const cotocoa_error *error)
{
    int initialized=0,finalized=0,rank=-1;
    MPI_Initialized(&initialized); MPI_Finalized(&finalized);
    if(initialized && !finalized) MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    const char *program=ctx && ctx->program>=0 ? ctx->plan.programs[ctx->program].name : "coupler/startup";
    fprintf(stderr,"cotocoa: rank=%d program=%s: %s\n",rank,program,
            error ? error->message : "unrecoverable error");
    fflush(stderr);
    if(initialized && !finalized) MPI_Abort(MPI_COMM_WORLD,EXIT_FAILURE);
    exit(EXIT_FAILURE);
}
static void collective_check(cotocoa_context *ctx,int bad,cotocoa_error *error,MPI_Comm comm)
{
    int any;
    MPI_Allreduce(&bad,&any,1,MPI_INT,MPI_MAX,comm);
    if(any) {
        if(!bad) fail(error,COTOCOA_INVALID,"another rank failed collective validation");
        cotocoa_abort(ctx,error);
    }
}
static MPI_Datatype mpi_type(int dtype)
{ return dtype==COTOCOA_FLOAT64 ? MPI_DOUBLE : (dtype==COTOCOA_FLOAT32 ? MPI_FLOAT : MPI_INT32_T); }
static void put64(int *h,int index,uint64_t value)
{
    uint32_t words[2]={(uint32_t)value,(uint32_t)(value>>32)};
    memcpy(h+index,words,sizeof(words));
}
static uint64_t get64(const int *h,int index)
{
    uint32_t words[2]; memcpy(words,h+index,sizeof(words));
    return words[0] | ((uint64_t)words[1]<<32);
}
static void digest_words(const char *digest,int *out)
{
    unsigned char bytes[32];
    for(int i=0;i<32;++i) {
        unsigned int byte; sscanf(digest+i*2,"%2x",&byte); bytes[i]=(unsigned char)byte;
    }
    memcpy(out,bytes,sizeof(bytes));
}
static cotocoa_array_view make_view(const void *data,int dtype,int ndim,const int64_t *shape,int order)
{
    cotocoa_array_view v={0}; v.data=data; v.dtype=dtype; v.ndim=ndim;
    if(!shape || ndim<1 || ndim>3 || (order!=COTOCOA_ORDER_C && order!=COTOCOA_ORDER_F)) return v;
    for(int d=0;d<3;++d) v.shape[d]=d<ndim ? shape[d] : 1;
    int64_t stride=(int64_t)ctco_itemsize(dtype);
    for(int d=0;d<ndim;++d) {
        int i=order==COTOCOA_ORDER_F ? d : ndim-1-d;
        if(shape[i]<1 || shape[i]>INT64_MAX/stride) { memset(v.shape,0,sizeof(v.shape)); return v; }
        v.strides[i]=stride; stride*=shape[i];
    }
    return v;
}
cotocoa_array_view cotocoa_view_f64(const double *p,int n,const int64_t *s,int order)
{ return make_view(p,COTOCOA_FLOAT64,n,s,order); }
cotocoa_array_view cotocoa_view_f32(const float *p,int n,const int64_t *s,int order)
{ return make_view(p,COTOCOA_FLOAT32,n,s,order); }
cotocoa_array_view cotocoa_view_i32(const int32_t *p,int n,const int64_t *s,int order)
{ return make_view(p,COTOCOA_INT32,n,s,order); }
cotocoa_send_options cotocoa_send_defaults(void)
{ cotocoa_send_options o={0}; return o; }

static void register_route(cotocoa_context *ctx,int index)
{
    ctco_connection *c=&ctx->plan.connections[index]; route_state *r=&ctx->routes[index];
    size_t count=c->producer==ctx->program ? c->elements*(size_t)c->slots : 0;
    if(c->dtype==COTOCOA_FLOAT64) {
        if(ctx->role==CTCO_PRODUCER) CTCAR_regarea_real8((double *)r->buffer,count,&r->area);
        else if(ctx->role==CTCO_COUPLER) CTCAC_regarea_real8(&r->area);
        else CTCAW_regarea_real8(&r->area);
    } else if(c->dtype==COTOCOA_FLOAT32) {
        if(ctx->role==CTCO_PRODUCER) CTCAR_regarea_real4((float *)r->buffer,count,&r->area);
        else if(ctx->role==CTCO_COUPLER) CTCAC_regarea_real4(&r->area);
        else CTCAW_regarea_real4(&r->area);
    } else {
        if(ctx->role==CTCO_PRODUCER) CTCAR_regarea_int((int *)r->buffer,count,&r->area);
        else if(ctx->role==CTCO_COUPLER) CTCAC_regarea_int(&r->area);
        else CTCAW_regarea_int(&r->area);
    }
}
int cotocoa_open(const char *name,const char *config,cotocoa_context **out,cotocoa_error *error)
{
    cotocoa_error fallback={0}; if(!error) error=&fallback;
    if(!out || !name || !config) return fail(error,COTOCOA_INVALID,"open requires name, config and output");
    *out=NULL;
    int initialized,finalized,world_rank,world_size;
    MPI_Finalized(&finalized);
    if(finalized || context_used) return fail(error,COTOCOA_STATE,"only one Context lifetime is supported per process");
    MPI_Initialized(&initialized); if(!initialized) MPI_Init(NULL,NULL);
    MPI_Comm_rank(MPI_COMM_WORLD,&world_rank); MPI_Comm_size(MPI_COMM_WORLD,&world_size);
    cotocoa_context *ctx=calloc(1,sizeof(*ctx));
    if(!ctx) fail(error,COTOCOA_MEMORY,"cannot allocate Context");
    collective_check(ctx,!ctx,error,MPI_COMM_WORLD);
    ctx->program=-1; ctx->owns_mpi=!initialized;
    const char *path=getenv("COTOCOA_MANIFEST"); if(!path || !*path) path=config;
    int bad=ctco_plan_read(path,&ctx->plan,error)!=COTOCOA_OK;
    collective_check(ctx,bad,error,MPI_COMM_WORLD);
    ctco_plan *p=&ctx->plan;
    uint64_t low,high;
    MPI_Allreduce(&p->file_hash,&low,1,MPI_UINT64_T,MPI_MIN,MPI_COMM_WORLD);
    MPI_Allreduce(&p->file_hash,&high,1,MPI_UINT64_T,MPI_MAX,MPI_COMM_WORLD);
    const char *expected_name="__coupler__"; ctx->role=CTCO_COUPLER;
    for(int i=0;i<p->program_count;++i) {
        ctco_program *a=&p->programs[i];
        if(world_rank>=a->world_offset && world_rank<a->world_offset+a->ranks) {
            ctx->program=i; ctx->role=a->role; expected_name=a->name; break;
        }
    }
    bad=low!=high || world_size!=p->world_size || strcmp(name,expected_name);
    if(bad) fail(error,COTOCOA_INVALID,"startup mismatch: program=%s expected=%s ranks=%d expected_ranks=%d or manifest differs",
                 name,expected_name,world_size,p->world_size);
    collective_check(ctx,bad,error,MPI_COMM_WORLD);
    uint32_t endian=1;
    int abi[6]={(int)sizeof(int),(int)sizeof(float),(int)sizeof(double),(int)sizeof(size_t),
                *(unsigned char *)&endian,(int)sizeof(MPI_Fint)},abi_low[6],abi_high[6];
    MPI_Allreduce(abi,abi_low,6,MPI_INT,MPI_MIN,MPI_COMM_WORLD);
    MPI_Allreduce(abi,abi_high,6,MPI_INT,MPI_MAX,MPI_COMM_WORLD);
    bad=memcmp(abi_low,abi_high,sizeof(abi_low)) || sizeof(size_t)!=8 || sizeof(MPI_Fint)!=sizeof(int);
    if(bad) fail(error,COTOCOA_INVALID,"incompatible native MPI/data ABI across ranks");
    collective_check(ctx,bad,error,MPI_COMM_WORLD);
    ctx->routes=calloc((size_t)p->connection_count,sizeof(route_state));
    if(!ctx->routes) fail(error,COTOCOA_MEMORY,"cannot allocate route states");
    collective_check(ctx,!ctx->routes,error,MPI_COMM_WORLD);
    for(int i=0;i<p->connection_count;++i) {
        ctco_connection *c=&p->connections[i]; route_state *r=&ctx->routes[i];
        bad=c->mode!=CTCO_SNAPSHOT;
        if(bad) fail(error,COTOCOA_INVALID,"delivery/memory policy is not enabled in this build");
        collective_check(ctx,bad,error,MPI_COMM_WORLD);
        if(ctx->role==CTCO_PRODUCER && c->producer==ctx->program) {
            r->buffer=calloc((size_t)c->slots,c->bytes); r->handles=calloc((size_t)c->slots,sizeof(int64_t));
            bad=!r->buffer || !r->handles;
        } else if(ctx->role==CTCO_CONSUMER && c->consumer==ctx->program) {
            r->received_sequences=calloc((size_t)p->programs[c->producer].ranks,sizeof(int64_t));
            bad=!r->received_sequences;
        } else bad=0;
        if(bad) fail(error,COTOCOA_MEMORY,"cannot allocate buffers for %s",c->name);
        collective_check(ctx,bad,error,MPI_COMM_WORLD);
    }
    context_used=1;
    if(ctx->role==CTCO_PRODUCER) CTCAR_init_detail(p->connection_count,p->max_requests,HEADER_WORDS);
    else if(ctx->role==CTCO_COUPLER) CTCAC_init_detail(p->connection_count,p->queue_capacity,HEADER_WORDS,1,1);
    else CTCAW_init_detail(ctx->program+1,p->programs[ctx->program].group_size,p->connection_count,HEADER_WORDS);
    for(int i=0;i<p->connection_count;++i) register_route(ctx,i);
    if(ctx->role==CTCO_PRODUCER) {
        MPI_Comm_rank(CTCA_subcomm,&ctx->source_rank);
        MPI_Comm_split(CTCA_subcomm,ctx->program,world_rank,&ctx->comm);
    } else MPI_Comm_dup(CTCA_subcomm,&ctx->comm);
    MPI_Comm_rank(ctx->comm,&ctx->rank); *out=ctx;
    return COTOCOA_OK;
}
MPI_Comm cotocoa_comm(const cotocoa_context *ctx) { return ctx ? ctx->comm : MPI_COMM_NULL; }
MPI_Fint cotocoa_comm_f(const cotocoa_context *ctx) { return MPI_Comm_c2f(cotocoa_comm(ctx)); }

static int validate_array(const ctco_connection *c,const cotocoa_array_view *a,
                           const cotocoa_send_options *o,cotocoa_error *error)
{
    if(!a || !a->data || a->dtype!=c->dtype || a->ndim!=c->ndim)
        return fail(error,COTOCOA_INVALID,"send %s: array type/rank differs from schema",c->name);
    if(o->has_time && !isfinite(o->time)) return fail(error,COTOCOA_INVALID,"send: non-finite time");
    uint64_t span=0;
    for(int d=0;d<3;++d) {
        if(a->shape[d]!=c->shape[d] || o->offset[d]<0 || o->offset[d]>c->global_shape[d]-c->shape[d] || a->strides[d]==INT64_MIN)
            return fail(error,COTOCOA_INVALID,"send %s: invalid shape/offset at axis %d",c->name,d);
        uint64_t stride=(uint64_t)(a->strides[d]<0 ? -a->strides[d] : a->strides[d]);
        if(a->shape[d]>1 && stride>(PTRDIFF_MAX-span)/(uint64_t)(a->shape[d]-1))
            return fail(error,COTOCOA_INVALID,"send %s: overflowing stride",c->name);
        span+=(uint64_t)(a->shape[d]-1)*stride;
    }
    return COTOCOA_OK;
}
static int validate_port(cotocoa_context *ctx,const char *port,int kind,const cotocoa_array_view *a,
                          const cotocoa_send_options *o,int *matches,cotocoa_error *error)
{
    *matches=0;
    if(!ctx || ctx->role!=CTCO_PRODUCER || !port) return fail(error,COTOCOA_STATE,"send/submit requires a producer Context and port");
    for(int i=0;i<ctx->plan.connection_count;++i) {
        ctco_connection *c=&ctx->plan.connections[i];
        if(c->producer!=ctx->program || strcmp(c->source_port,port) || c->kind!=kind) continue;
        ++*matches;
        if(ctx->routes[i].sequence==INT64_MAX) return fail(error,COTOCOA_STATE,"connection sequence exhausted");
        int rc=validate_array(c,a,o,error); if(rc) return rc;
    }
    if(!*matches) return fail(error,COTOCOA_INVALID,"no %s output port '%s'",kind==CTCO_TASK ? "task" : "stream",port);
    return COTOCOA_OK;
}
static int64_t send_one(cotocoa_context *ctx,int index,const cotocoa_array_view *a,const cotocoa_send_options *o)
{
    ctco_connection *c=&ctx->plan.connections[index]; route_state *r=&ctx->routes[index];
    int slot=r->next_slot;
    if(c->delivery==CTCO_LATEST) {
        slot=-1;
        for(int s=0;s<c->slots;++s)
            if(!r->handles[s] || CTCAR_test(r->handles[s])) { slot=s; break; }
        if(slot<0) {
            int control[HEADER_WORDS]={0};
            control[0]=MAGIC; control[1]=2; control[2]=index+1;
            control[4]=ctx->source_rank; control[47]=1;
            digest_words(ctx->plan.digest,control+26);
            CTCAR_sendreq(control,HEADER_WORDS);
            while(slot<0) for(int s=0;s<c->slots;++s)
                if(CTCAR_test(r->handles[s])) { slot=s; break; }
        }
    } else if(r->handles[slot]) CTCAR_wait(r->handles[slot]);
    char *dest=r->buffer+(size_t)slot*c->bytes;
    const char *base=a->data; size_t n=0,unit=ctco_itemsize(c->dtype);
    for(int64_t k=0;k<c->shape[2];++k) for(int64_t j=0;j<c->shape[1];++j) for(int64_t i=0;i<c->shape[0];++i)
        memcpy(dest+n++*unit,base+i*a->strides[0]+j*a->strides[1]+k*a->strides[2],unit);
    ctca_sync_area(r->area);
    int h[HEADER_WORDS]={0};
    h[0]=MAGIC; h[1]=2; h[2]=index+1; h[3]=slot; h[4]=ctx->source_rank; h[5]=!!o->has_time;
    put64(h,6,(uint64_t)++r->sequence); put64(h,8,(uint64_t)o->step);
    uint64_t time_bits; memcpy(&time_bits,&o->time,sizeof(time_bits)); put64(h,10,time_bits);
    for(int d=0;d<3;++d) { put64(h,12+d*2,(uint64_t)o->offset[d]); put64(h,18+d*2,(uint64_t)c->shape[d]); }
    put64(h,24,c->elements); digest_words(ctx->plan.digest,h+26); h[34]=c->dtype; h[35]=c->ndim;
    CTCAR_sendreq_hdl(h,HEADER_WORDS,&r->handles[slot]); r->next_slot=(slot+1)%c->slots;
    return r->handles[slot];
}
int cotocoa_send(cotocoa_context *ctx,const char *name,const cotocoa_array_view *a,
                 const cotocoa_send_options *o,cotocoa_error *error)
{
    cotocoa_send_options defaults=cotocoa_send_defaults(); if(!o) o=&defaults;
    int matches,rc=validate_port(ctx,name,CTCO_STREAM,a,o,&matches,error); if(rc) return rc;
    for(int i=0;i<ctx->plan.connection_count;++i) {
        ctco_connection *c=&ctx->plan.connections[i];
        if(c->producer==ctx->program && c->kind==CTCO_STREAM && !strcmp(c->source_port,name)) send_one(ctx,i,a,o);
    }
    return COTOCOA_OK;
}
int cotocoa_submit(cotocoa_context *ctx,const char *name,const cotocoa_array_view *a,
                   const cotocoa_send_options *o,cotocoa_ticket **out,cotocoa_error *error)
{
    if(!out) return fail(error,COTOCOA_INVALID,"submit requires a ticket output"); *out=NULL;
    cotocoa_send_options defaults=cotocoa_send_defaults(); if(!o) o=&defaults;
    int matches,rc=validate_port(ctx,name,CTCO_TASK,a,o,&matches,error); if(rc) return rc;
    if(matches!=1) return fail(error,COTOCOA_INVALID,"task port must have one destination");
    cotocoa_ticket *ticket=calloc(1,sizeof(*ticket));
    if(!ticket) return fail(error,COTOCOA_MEMORY,"cannot allocate task ticket");
    for(int i=0;i<ctx->plan.connection_count;++i) {
        ctco_connection *c=&ctx->plan.connections[i];
        if(c->producer==ctx->program && c->kind==CTCO_TASK && !strcmp(c->source_port,name)) {
            ticket->handle=send_one(ctx,i,a,o); break;
        }
    }
    ticket->context=ctx; ticket->references=1; ticket->next=ctx->tickets; ctx->tickets=ticket; *out=ticket;
    return COTOCOA_OK;
}
int cotocoa_ticket_test(cotocoa_ticket *t,int *done,cotocoa_error *error)
{
    if(!t || !done) return fail(error,COTOCOA_INVALID,"ticket test requires ticket and output");
    if(!t->completed && t->context) t->completed=CTCAR_test(t->handle);
    *done=t->completed; return COTOCOA_OK;
}
int cotocoa_ticket_wait(cotocoa_ticket *t,cotocoa_error *error)
{
    if(!t) return fail(error,COTOCOA_INVALID,"wait requires a task ticket");
    if(!t->completed && t->context) CTCAR_wait(t->handle);
    t->completed=1; return COTOCOA_OK;
}
void cotocoa_ticket_retain(cotocoa_ticket *t) { if(t) ++t->references; }
void cotocoa_ticket_release(cotocoa_ticket *t)
{
    if(!t || --t->references) return;
    if(t->context) {
        cotocoa_ticket **link=&t->context->tickets;
        while(*link && *link!=t) link=&(*link)->next;
        if(*link) *link=t->next;
    }
    free(t);
}
int cotocoa_flush(cotocoa_context *ctx,cotocoa_error *error)
{
    if(!ctx || ctx->role!=CTCO_PRODUCER) return fail(error,COTOCOA_STATE,"flush requires a producer Context");
    for(int i=0;i<ctx->plan.connection_count;++i) {
        ctco_connection *c=&ctx->plan.connections[i]; route_state *r=&ctx->routes[i];
        if(c->producer!=ctx->program) continue;
        for(int s=0;s<c->slots;++s) { if(r->handles[s]) CTCAR_wait(r->handles[s]); r->handles[s]=0; }
    }
    return COTOCOA_OK;
}
static int validate_header(cotocoa_context *ctx,const int *h,int source,cotocoa_error *error)
{
    int digest[8]; digest_words(ctx->plan.digest,digest);
    if(h[0]!=MAGIC || h[1]!=2 || h[47]!=0 || h[2]<1 || h[2]>ctx->plan.connection_count)
        return fail(error,COTOCOA_INVALID,"invalid protocol/version/connection from source %d",source);
    ctco_connection *c=&ctx->plan.connections[h[2]-1]; ctco_program *a=&ctx->plan.programs[c->producer];
    if(h[3]<0 || h[3]>=c->slots || source<a->source_offset || source>=a->source_offset+a->ranks || h[4]!=source ||
       (h[5]!=0 && h[5]!=1) || get64(h,6)<1 || get64(h,6)>INT64_MAX || get64(h,24)!=c->elements ||
       memcmp(h+26,digest,sizeof(digest)) || h[34]!=c->dtype || h[35]!=c->ndim)
        return fail(error,COTOCOA_INVALID,"invalid header for %s from source %d",c->name,source);
    for(int d=0;d<3;++d) if(get64(h,18+d*2)!=(uint64_t)c->shape[d] ||
        get64(h,12+d*2)>(uint64_t)(c->global_shape[d]-c->shape[d]))
            return fail(error,COTOCOA_INVALID,"invalid shape/offset for %s",c->name);
    return COTOCOA_OK;
}
static int receive_impl(cotocoa_context *ctx,const char *name,int kind,cotocoa_message **out,cotocoa_error *error)
{
    cotocoa_error fallback={0}; if(!error) error=&fallback;
    if(!ctx || ctx->role!=CTCO_CONSUMER) return fail(error,COTOCOA_STATE,"receive requires a consumer Context");
    int bad=!out || ctx->active, specific=name && strcmp(name,"*");
    for(int i=0;i<ctx->plan.connection_count;++i) {
        ctco_connection *c=&ctx->plan.connections[i]; if(c->consumer!=ctx->program) continue;
        if((specific && strcmp(name,c->target_port)) || (kind>=0 && c->kind!=kind)) bad=1;
    }
    if(bad) fail(error,COTOCOA_STATE,"receive: incomplete message or multiple ports/kinds; use receive-any");
    collective_check(ctx,bad,error,ctx->comm); *out=NULL;
    if(ctx->ended) return COTOCOA_END;
    int h[HEADER_WORDS],source; CTCAW_pollreq(&source,h,HEADER_WORDS);
    if(CTCAW_isfin()) { ctx->ended=1; return COTOCOA_END; }
    bad=validate_header(ctx,h,source,error)!=COTOCOA_OK; collective_check(ctx,bad,error,ctx->comm);
    int index=h[2]-1; ctco_connection *c=&ctx->plan.connections[index]; route_state *r=&ctx->routes[index];
    ctco_program *a=&ctx->plan.programs[c->producer]; int local_source=source-a->source_offset;
    int64_t sequence=(int64_t)get64(h,6);
    bad=c->consumer!=ctx->program || !r->received_sequences;
    if(!bad) bad=sequence<=r->received_sequences[local_source] ||
        (c->kind==CTCO_STREAM && c->delivery==CTCO_ALL && sequence!=r->received_sequences[local_source]+1);
    if(bad) fail(error,COTOCOA_INVALID,"wrong destination or out-of-order message on %s",c->name);
    collective_check(ctx,bad,error,ctx->comm);
    cotocoa_message *message=calloc(1,sizeof(*message)); if(message) message->data=malloc(c->bytes);
    bad=!message || !message->data;
    if(bad) fail(error,COTOCOA_MEMORY,"cannot allocate snapshot for %s",c->name);
    collective_check(ctx,bad,error,ctx->comm);
    if(ctx->rank==0) {
        size_t offset=(size_t)h[3]*c->elements;
        if(c->dtype==COTOCOA_FLOAT64) CTCAW_readarea_real8(r->area,source,offset,c->elements,message->data);
        else if(c->dtype==COTOCOA_FLOAT32) CTCAW_readarea_real4(r->area,source,offset,c->elements,message->data);
        else CTCAW_readarea_int(r->area,source,offset,c->elements,message->data);
    }
    MPI_Bcast(message->data,(int)c->elements,mpi_type(c->dtype),0,ctx->comm); message->context=ctx;
    cotocoa_metadata *m=&message->metadata;
    m->step=(int64_t)get64(h,8); m->sequence=sequence; m->source_rank=local_source;
    m->has_time=h[5]; m->dtype=c->dtype; m->ndim=c->ndim;
    m->kind=c->kind; m->connection_id=index; m->source_count=a->ranks;
    uint64_t bits=get64(h,10); memcpy(&m->time,&bits,sizeof(m->time));
    memcpy(m->shape,c->shape,sizeof(m->shape)); memcpy(m->global_shape,c->global_shape,sizeof(m->global_shape));
    memcpy(m->axes,c->axes,sizeof(m->axes)); memcpy(m->units,c->units,sizeof(m->units));
    memcpy(m->time_unit,c->time_unit,sizeof(m->time_unit)); memcpy(m->name,c->name,sizeof(m->name));
    memcpy(m->port,c->target_port,sizeof(m->port)); memcpy(m->source,a->name,sizeof(m->source));
    for(int d=0;d<3;++d) m->offset[d]=(int64_t)get64(h,12+d*2);
    r->received_sequences[local_source]=sequence; ctx->active=message; *out=message;
    return COTOCOA_OK;
}
int cotocoa_receive(cotocoa_context *ctx,const char *name,cotocoa_message **out,cotocoa_error *e)
{ return receive_impl(ctx,name,-1,out,e); }
int cotocoa_next_task(cotocoa_context *ctx,const char *name,cotocoa_message **out,cotocoa_error *e)
{ return receive_impl(ctx,name,CTCO_TASK,out,e); }
int cotocoa_message_view(const cotocoa_message *m,cotocoa_array_view *out,cotocoa_error *e)
{
    if(!m || !out) return fail(e,COTOCOA_INVALID,"message_view requires message and output");
    *out=make_view(m->data,m->metadata.dtype,m->metadata.ndim,m->metadata.shape,COTOCOA_ORDER_F); return COTOCOA_OK;
}
int cotocoa_message_metadata(const cotocoa_message *m,cotocoa_metadata *out,cotocoa_error *e)
{ if(!m || !out) return fail(e,COTOCOA_INVALID,"metadata requires message and output"); *out=m->metadata; return COTOCOA_OK; }
int cotocoa_complete(cotocoa_message *m,cotocoa_error *e)
{
    if(!m) return fail(e,COTOCOA_INVALID,"complete requires a message");
    if(m->completed) return fail(e,COTOCOA_STATE,"message already completed");
    CTCAW_complete(); m->context->active=NULL; m->context=NULL; m->completed=1; return COTOCOA_OK;
}
void cotocoa_message_release(cotocoa_message *m)
{
    if(!m) return;
    if(!m->completed) {
        cotocoa_error e; fail(&e,COTOCOA_STATE,"cannot release an incomplete message"); cotocoa_abort(m->context,&e);
    }
    free(m->data); free(m);
}
int cotocoa_couple(cotocoa_context *ctx,cotocoa_error *e)
{
    cotocoa_error fallback={0}; if(!e) e=&fallback;
    if(!ctx || ctx->role!=CTCO_COUPLER) return fail(e,COTOCOA_STATE,"couple requires a Coupler Context");
    while(!ctx->ended) {
        int info[CTCAC_REQINFOITEMS],source,h[HEADER_WORDS]; CTCAC_pollreq(info,&source,h,HEADER_WORDS);
        if(CTCAC_isfin()) { ctx->ended=1; break; }
        if(h[47]==1) {
            int digest[8]; digest_words(ctx->plan.digest,digest);
            if(h[0]!=MAGIC || h[1]!=2 || h[2]<1 || h[2]>ctx->plan.connection_count ||
               h[4]!=source || memcmp(h+26,digest,sizeof(digest))) {
                fail(e,COTOCOA_INVALID,"invalid latest control request"); cotocoa_abort(ctx,e);
            }
            ctco_connection *c=&ctx->plan.connections[h[2]-1];
            ctco_program *p=&ctx->plan.programs[c->producer];
            if(c->delivery!=CTCO_LATEST || source<p->source_offset || source>=p->source_offset+p->ranks) {
                fail(e,COTOCOA_INVALID,"invalid latest control source/policy"); cotocoa_abort(ctx,e);
            }
            ctca_drop_queued(h[2],source);
            continue;
        }
        if(validate_header(ctx,h,source,e)) cotocoa_abort(ctx,e);
        if(ctx->plan.connections[h[2]-1].delivery==CTCO_LATEST) ctca_drop_queued(h[2],source);
        CTCAC_enqreq(info,ctx->plan.connections[h[2]-1].consumer+1,h,HEADER_WORDS);
    }
    return COTOCOA_OK;
}
int cotocoa_close(cotocoa_context *ctx,cotocoa_error *e)
{
    cotocoa_error fallback={0}; if(!e) e=&fallback;
    if(!ctx) return fail(e,COTOCOA_STATE,"close requires an open Context");
    if(ctx->role!=CTCO_PRODUCER && (!ctx->ended || ctx->active)) {
        fail(e,COTOCOA_STATE,"consumer/Coupler must receive end-of-stream before close"); cotocoa_abort(ctx,e);
    }
    if(ctx->role==CTCO_PRODUCER) cotocoa_flush(ctx,e);
    MPI_Comm_free(&ctx->comm);
    if(ctx->role==CTCO_PRODUCER) CTCAR_finalize();
    else if(ctx->role==CTCO_COUPLER) CTCAC_finalize(); else CTCAW_finalize();
    cotocoa_ticket *t=ctx->tickets;
    while(t) { cotocoa_ticket *next=t->next; t->completed=1; t->context=NULL; t->next=NULL; t=next; }
    for(int i=0;i<ctx->plan.connection_count;++i) {
        free(ctx->routes[i].buffer); free(ctx->routes[i].handles); free(ctx->routes[i].received_sequences);
    }
    int owns=ctx->owns_mpi; free(ctx->routes); free(ctx); if(owns) MPI_Finalize(); return COTOCOA_OK;
}
