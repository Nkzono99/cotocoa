#include "cotocoa.h"
#include <stdio.h>
int main(void)
{
    cotocoa_assembler *a=NULL; cotocoa_error e={0};
    if(cotocoa_assembler_create(2,1024,&a,&e) || cotocoa_assembler_finish(a,&e)) return 1;
    cotocoa_assembler_release(a);
    puts("PASS installed C API"); return 0;
}
