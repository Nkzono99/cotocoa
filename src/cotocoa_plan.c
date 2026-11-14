#include "cotocoa_plan.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t ctco_itemsize(int dtype)
{
    return dtype == COTOCOA_FLOAT64 ? 8 :
        (dtype == COTOCOA_FLOAT32 || dtype == COTOCOA_INT32 ? 4 : 0);
}

static int fail(cotocoa_error *error, const char *message, const char *path)
{
    if (error) {
        error->code = COTOCOA_INVALID;
        snprintf(error->message,sizeof(error->message),"%s: %.350s",message,path ? path : "");
    }
    return COTOCOA_INVALID;
}

static int line(FILE *file, char *out, size_t capacity)
{
    char buffer[512];
    if (!fgets(buffer,sizeof(buffer),file)) return 0;
    size_t n = strlen(buffer);
    if (!n || buffer[n-1]!='\n' || n>capacity) return 0;
    buffer[n-1]='\0';
    memcpy(out,buffer,n);
    return 1;
}

static int name(const char *text)
{
    if (!*text || strlen(text)>63 || !strcmp(text,"__coupler__")) return 0;
    for (size_t i=0;text[i];++i) {
        unsigned char c=(unsigned char)text[i];
        if (!(c=='_' || (c>='a' && c<='z') || (c>='A' && c<='Z') ||
              (i && ((c>='0' && c<='9') || c=='-')))) return 0;
    }
    return 1;
}

/* Parse decimal integers with range checking, including malformed manifests
 * that did not come from the Python compiler. Never overflow scanf's int. */
static int numbers(FILE *file, int64_t *values, size_t count)
{
    char buffer[512], *cursor=buffer;
    if (!line(file,buffer,sizeof(buffer))) return 0;
    for (size_t i=0;i<count;++i) {
        while (isspace((unsigned char)*cursor)) ++cursor;
        if (!*cursor) return 0;
        char *end;
        errno=0;
        intmax_t value=strtoimax(cursor,&end,10);
        if (errno || end==cursor || (*end && !isspace((unsigned char)*end)) ||
            value<INT64_MIN || value>INT64_MAX) return 0;
        values[i]=(int64_t)value;
        cursor=end;
    }
    while (isspace((unsigned char)*cursor)) ++cursor;
    return !*cursor;
}

static int bound(int64_t value, int low, int high)
{ return value>=low && value<=high; }

static int validate(ctco_plan *p)
{
    int64_t world=1, sources=0, queued=0;
    int requests[COTOCOA_MAX_PROGRAMS]={0};
    int incoming[COTOCOA_MAX_PROGRAMS]={0}, outgoing[COTOCOA_MAX_PROGRAMS]={0};
    int seen_consumer=0;
    for(int i=0;i<p->program_count;++i) {
        ctco_program *a=&p->programs[i];
        if(!name(a->name) || a->ranks<1 || a->ranks>1000000 || a->group_size<1 ||
           a->ranks%a->group_size || (a->role!=CTCO_PRODUCER && a->role!=CTCO_CONSUMER)) return 0;
        for(int j=0;j<i;++j) if(!strcmp(a->name,p->programs[j].name)) return 0;
        if(a->role==CTCO_PRODUCER) {
            if(seen_consumer || a->group_size!=a->ranks) return 0;
            a->world_offset=(int)sources;
            a->source_offset=(int)sources;
            sources+=a->ranks;
        } else seen_consumer=1;
        world+=a->ranks;
    }
    if(!sources || sources>524288 || !seen_consumer || world>INT_MAX) return 0;
    p->source_count=(int)sources;
    p->coupler_rank=(int)sources;
    p->world_size=(int)world;
    int world_offset=(int)sources+1;
    for(int i=0;i<p->program_count;++i) if(p->programs[i].role==CTCO_CONSUMER) {
        p->programs[i].world_offset=world_offset;
        world_offset+=p->programs[i].ranks;
    }
    for(int i=0;i<p->connection_count;++i) {
        ctco_connection *c=&p->connections[i];
        if(!name(c->name) || !name(c->source_port) || !name(c->target_port) ||
           c->producer<0 || c->producer>=p->program_count ||
           c->consumer<0 || c->consumer>=p->program_count ||
           p->programs[c->producer].role!=CTCO_PRODUCER ||
           p->programs[c->consumer].role!=CTCO_CONSUMER ||
           !ctco_itemsize(c->dtype) || c->ndim<1 || c->ndim>3 ||
           c->slots<1 || c->slots>4094 || c->kind<0 || c->kind>1 ||
           c->delivery<0 || c->delivery>1 || c->mode<0 || c->mode>1) return 0;
        if(c->kind==CTCO_TASK && c->delivery!=CTCO_ALL) return 0;
        if(c->kind==CTCO_STREAM && p->programs[c->consumer].group_size!=p->programs[c->consumer].ranks) return 0;
        if(c->delivery==CTCO_LATEST && c->slots<2) return 0;
        c->elements=1;
        for(int d=0;d<3;++d) {
            if(c->shape[d]<1 || c->global_shape[d]<c->shape[d] || !name(c->axes[d]) ||
               (d>=c->ndim && (c->shape[d]!=1 || c->global_shape[d]!=1)) ||
               c->shape[d]>INT_MAX/8 || c->elements>(size_t)(INT_MAX/8)/(size_t)c->shape[d]) return 0;
            for(int k=0;k<d && d<c->ndim;++k) if(!strcmp(c->axes[d],c->axes[k])) return 0;
            c->elements*=(size_t)c->shape[d];
        }
        c->bytes=c->elements*ctco_itemsize(c->dtype);
        if(c->bytes>SIZE_MAX/(size_t)c->slots || requests[c->producer]>4094-c->slots) return 0;
        requests[c->producer]+=c->slots;
        outgoing[c->producer]++;
        incoming[c->consumer]++;
        queued+=(int64_t)c->slots*p->programs[c->producer].ranks;
        if(queued>INT_MAX-1) return 0;
        for(int j=0;j<i;++j) {
            ctco_connection *prior=&p->connections[j];
            if(!strcmp(c->name,prior->name)) return 0;
            if(c->producer==prior->producer && !strcmp(c->source_port,prior->source_port)) {
                if(c->kind==CTCO_TASK || prior->kind==CTCO_TASK || c->dtype!=prior->dtype ||
                   c->ndim!=prior->ndim || c->mode!=prior->mode ||
                   memcmp(c->shape,prior->shape,sizeof(c->shape)) ||
                   memcmp(c->global_shape,prior->global_shape,sizeof(c->global_shape)) ||
                   memcmp(c->axes,prior->axes,sizeof(c->axes)) ||
                   strcmp(c->units,prior->units) || strcmp(c->time_unit,prior->time_unit)) return 0;
                if(c->consumer==prior->consumer && !strcmp(c->target_port,prior->target_port)) return 0;
            }
        }
    }
    p->max_requests=0;
    for(int i=0;i<p->program_count;++i) {
        if(p->programs[i].role==CTCO_PRODUCER && !outgoing[i]) return 0;
        if(p->programs[i].role==CTCO_CONSUMER && !incoming[i]) return 0;
        if(requests[i]>p->max_requests) p->max_requests=requests[i];
    }
    p->queue_capacity=(int)queued;
    return 1;
}

int ctco_plan_read(const char *path, ctco_plan *p, cotocoa_error *error)
{
    FILE *file=path ? fopen(path,"rb") : NULL;
    char buffer[512];
    int64_t values[8];
    int ok=0, ch, version;
    if(!file) return fail(error,"cannot open native manifest",path);
    memset(p,0,sizeof(*p));
    p->file_hash=UINT64_C(14695981039346656037);
    size_t length=0;
    while((ch=fgetc(file))!=EOF) {
        if(++length>1048576) goto done;
        p->file_hash=(p->file_hash^(unsigned char)ch)*UINT64_C(1099511628211);
    }
    rewind(file);
    if(!line(file,buffer,sizeof(buffer))) goto done;
    version=!strcmp(buffer,"COTOCOA_MANIFEST 3") ? 3 :
        (!strcmp(buffer,"COTOCOA_MANIFEST 2") ? 2 : (!strcmp(buffer,"COTOCOA_MANIFEST 1") ? 1 : 0));
    if(!version || !line(file,buffer,sizeof(buffer)) || strlen(buffer)!=64) goto done;
    for(int i=0;i<64;++i) if(!isxdigit((unsigned char)buffer[i]) || isupper((unsigned char)buffer[i])) goto done;
    memcpy(p->digest,buffer,65);
    if(version!=3) {
        /* Preserve the committed single-connection native formats. */
        p->program_count=2; p->connection_count=1;
        for(int i=0;i<2;++i) {
            ctco_program *a=&p->programs[i];
            if(!line(file,buffer,sizeof(buffer))) goto done;
            char *separator=strchr(buffer,' '), *end;
            if(!separator || separator==buffer || separator-buffer>63) goto done;
            *separator='\0'; strcpy(a->name,buffer);
            errno=0; intmax_t ranks=strtoimax(separator+1,&end,10);
            if(errno || end==separator+1 || *end || ranks<1 || ranks>1000000) goto done;
            a->ranks=(int)ranks;
            a->role=i ? CTCO_CONSUMER : CTCO_PRODUCER;
            a->group_size=a->ranks;
        }
        ctco_connection *c=&p->connections[0];
        c->producer=0; c->consumer=1; c->dtype=COTOCOA_FLOAT64; c->ndim=3;
        if(!line(file,c->name,sizeof(c->name)) || !line(file,c->source_port,sizeof(c->source_port)) ||
           !line(file,c->target_port,sizeof(c->target_port)) || !numbers(file,c->shape,3) ||
           !numbers(file,c->global_shape,3) || !numbers(file,values,1) || !bound(values[0],1,4094)) goto done;
        c->slots=(int)values[0];
        for(int d=0;d<3;++d) if(!line(file,c->axes[d],64)) goto done;
        if(!line(file,c->units,64) || !line(file,c->time_unit,64)) goto done;
        if(version==2) {
            if(!numbers(file,values,2) || !bound(values[0],1,3) || !bound(values[1],1,3)) goto done;
            c->dtype=(int)values[0]; c->ndim=(int)values[1];
        }
    } else {
        if(!line(file,buffer,sizeof(buffer))) goto done;
        char *end;
        errno=0; p->timeout=strtod(buffer,&end);
        if(errno || end==buffer || *end || !isfinite(p->timeout) || p->timeout<0) goto done;
        if(!numbers(file,values,2) || !bound(values[0],2,COTOCOA_MAX_PROGRAMS) ||
           !bound(values[1],1,COTOCOA_MAX_CONNECTIONS)) goto done;
        p->program_count=(int)values[0]; p->connection_count=(int)values[1];
        for(int i=0;i<p->program_count;++i) {
            ctco_program *a=&p->programs[i];
            if(!line(file,a->name,64) || !numbers(file,values,3) || !bound(values[0],0,2) ||
               !bound(values[1],1,1000000) || !bound(values[2],1,1000000)) goto done;
            a->role=(int)values[0]; a->ranks=(int)values[1]; a->group_size=(int)values[2];
        }
        for(int i=0;i<p->connection_count;++i) {
            ctco_connection *c=&p->connections[i];
            if(!line(file,c->name,64) || !numbers(file,values,2) ||
               !bound(values[0],0,p->program_count-1) || !bound(values[1],0,p->program_count-1)) goto done;
            c->producer=(int)values[0]; c->consumer=(int)values[1];
            if(!line(file,c->source_port,64) || !line(file,c->target_port,64) || !numbers(file,values,6)) goto done;
            if(!bound(values[0],1,3) || !bound(values[1],1,3) || !bound(values[2],1,4094) ||
               !bound(values[3],0,1) || !bound(values[4],0,1) || !bound(values[5],0,1)) goto done;
            c->dtype=(int)values[0]; c->ndim=(int)values[1]; c->slots=(int)values[2];
            c->kind=(int)values[3]; c->delivery=(int)values[4]; c->mode=(int)values[5];
            if(!numbers(file,c->shape,3) || !numbers(file,c->global_shape,3)) goto done;
            for(int d=0;d<3;++d) if(!line(file,c->axes[d],64)) goto done;
            if(!line(file,c->units,64) || !line(file,c->time_unit,64)) goto done;
        }
    }
    ok=fgetc(file)==EOF && validate(p);
done:
    fclose(file);
    return ok ? COTOCOA_OK : fail(error,"invalid or unsupported native manifest",path);
}
