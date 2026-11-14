#include "cotocoa_plan.h"
#include <stdio.h>
int main(int argc,char **argv)
{
    ctco_plan plan; cotocoa_error error={0};
    if(argc!=2) return 2;
    if(ctco_plan_read(argv[1],&plan,&error)) { fprintf(stderr,"%s\n",error.message); return 1; }
    printf("PASS manifest programs=%d connections=%d ranks=%d\n",plan.program_count,plan.connection_count,plan.world_size);
    return 0;
}
