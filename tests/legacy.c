#include "ctca.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int borrowed = getenv("COTOCOA_TEST_BORROWED") != NULL;
    if (argc != 2) return 2;
    if (borrowed) MPI_Init(&argc, &argv);
    int area, rank, source, params[2], info[CTCAC_REQINFOITEMS], count=0;
    double data[4], remote[4];
    if (!strcmp(argv[1], "producer")) CTCAR_init();
    else if (!strcmp(argv[1], "coupler")) CTCAC_init();
    else CTCAW_init(1,2);
    MPI_Comm_rank(CTCA_subcomm, &rank);
    if (!strcmp(argv[1], "producer")) CTCAR_regarea_real8(data,4,&area);
    else if (!strcmp(argv[1], "coupler")) CTCAC_regarea_real8(&area);
    else CTCAW_regarea_real8(&area);
    if (!strcmp(argv[1], "producer")) {
        for (int step=1;step<=16;++step) {
            int64_t handle;
            params[0]=rank; params[1]=step;
            for(int i=0;i<4;++i) data[i]=rank*1000+step*10+i;
            CTCAR_sendreq_withreal8_hdl(params,2,data,4,&handle);
            CTCAR_wait(handle);
        }
        CTCAR_finalize();
    } else if (!strcmp(argv[1], "coupler")) {
        for (;;) {
            CTCAC_pollreq_withreal8(info,&source,params,2,data,4);
            if (CTCAC_isfin()) break;
            CTCAC_enqreq_withreal8(info,1,params,2,data,4);
        }
        CTCAC_finalize();
    } else {
        for (;;) {
            CTCAW_pollreq_withreal8(&source,params,2,data,4);
            if (CTCAW_isfin()) break;
            CTCAW_readarea_real8(area,source,0,4,remote);
            for(int i=0;i<4;++i)
                if(data[i]!=params[0]*1000+params[1]*10+i || data[i]!=remote[i])
                    MPI_Abort(MPI_COMM_WORLD,1);
            CTCAW_complete();
            ++count;
        }
        if (count!=32) MPI_Abort(MPI_COMM_WORLD,1);
        printf("PASS legacy rank=%d messages=%d\n",rank,count);
        CTCAW_finalize();
    }
    int finalized;
    MPI_Finalized(&finalized);
    if (finalized == borrowed) return 1;
    if (borrowed) { MPI_Barrier(MPI_COMM_SELF); MPI_Finalize(); }
    return 0;
}
