#include "cotocoa.h"
#include "cotocoa_plan.h"
#include <stdio.h>

int main(int argc,char **argv)
{
    MPI_Init(&argc,&argv);
    int rank; MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    cotocoa_error error={0}; ctco_plan plan;
    if(argc!=2 || ctco_plan_read(argv[1],&plan,&error)) cotocoa_abort(NULL,&error);
    const char *name="__coupler__";
    int role=CTCO_COUPLER;
    for(int i=0;i<plan.program_count;++i) {
        ctco_program *p=&plan.programs[i];
        if(rank>=p->world_offset && rank<p->world_offset+p->ranks) { name=p->name; role=p->role; break; }
    }
    cotocoa_context *ctx=NULL;
    if(cotocoa_open(name,argv[1],&ctx,&error)) cotocoa_abort(ctx,&error);
    if(role==CTCO_COUPLER) {
        if(cotocoa_couple(ctx,&error)) cotocoa_abort(ctx,&error);
    } else if(role==CTCO_CONSUMER) {
        cotocoa_message *m=NULL;
        if(cotocoa_receive(ctx,NULL,&m,&error)!=COTOCOA_END) cotocoa_abort(ctx,&error);
    }
    if(cotocoa_close(ctx,&error)) cotocoa_abort(NULL,&error);
    if(!rank) printf("PASS probe: %d ranks, schema/MPI ABI, communicators, windows and collective shutdown\n",plan.world_size);
    MPI_Finalize(); return 0;
}
