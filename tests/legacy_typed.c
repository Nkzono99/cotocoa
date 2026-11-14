#include "ctca.h"
#include "ctca_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef union { int32_t i[4]; float f[4]; double d[4]; } payload;
int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    int dtype=atoi(getenv("COTOCOA_LEGACY_DTYPE")),area,rank,source,params[2],info[CTCAC_REQINFOITEMS];
    payload data;
    int producer=!strcmp(argv[1],"producer");
    if(producer) CTCAR_init(); else CTCAC_init();
    MPI_Comm_rank(CTCA_subcomm,&rank);
    if(producer) {
        if(dtype==0) CTCAR_regarea_int(data.i,4,&area);
        else if(dtype==1) CTCAR_regarea_real4(data.f,4,&area);
        else CTCAR_regarea_real8(data.d,4,&area);
        for(int step=1;step<=8;++step) {
            for(int i=0;i<4;++i) {
                int value=rank*1000+step*10+i;
                if(dtype==0) data.i[i]=value; else if(dtype==1) data.f[i]=value; else data.d[i]=value;
            }
            ctca_sync_area(area); params[0]=rank; params[1]=step; int64_t handle;
            if(dtype==0) CTCAR_sendreq_withint_hdl(params,2,data.i,4,&handle);
            else if(dtype==1) CTCAR_sendreq_withreal4_hdl(params,2,data.f,4,&handle);
            else CTCAR_sendreq_withreal8_hdl(params,2,data.d,4,&handle);
            CTCAR_wait(handle); ctca_sync_area(area);
            for(int i=0;i<4;++i) {
                double actual=dtype==0 ? data.i[i] : (dtype==1 ? data.f[i] : data.d[i]);
                if(actual!=-(rank*1000+step*10+i)) MPI_Abort(MPI_COMM_WORLD,1);
            }
        }
        CTCAR_finalize();
    } else {
        if(dtype==0) CTCAC_regarea_int(&area); else if(dtype==1) CTCAC_regarea_real4(&area); else CTCAC_regarea_real8(&area);
        for(;;) {
            if(dtype==0) CTCAC_pollreq_withint(info,&source,params,2,data.i,4);
            else if(dtype==1) CTCAC_pollreq_withreal4(info,&source,params,2,data.f,4);
            else CTCAC_pollreq_withreal8(info,&source,params,2,data.d,4);
            if(CTCAC_isfin()) break;
            if(dtype==0) CTCAC_enqreq_withint(info,1,params,2,data.i,4);
            else if(dtype==1) CTCAC_enqreq_withreal4(info,1,params,2,data.f,4);
            else CTCAC_enqreq_withreal8(info,1,params,2,data.d,4);
        }
        CTCAC_finalize();
    }
    puts("PASS legacy typed native producer/coupler"); return 0;
}
