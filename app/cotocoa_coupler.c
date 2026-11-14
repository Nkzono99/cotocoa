#include "cotocoa.h"

int main(int argc, char **argv)
{
    cotocoa_context *ctx = 0;
    cotocoa_error error = {0};
    if (cotocoa_open("__coupler__", argc > 1 ? argv[1] : "cotocoa.manifest", &ctx, &error))
        cotocoa_abort(ctx, &error);
    if (cotocoa_couple(ctx, &error)) cotocoa_abort(ctx, &error);
    if (cotocoa_close(ctx, &error)) cotocoa_abort(0, &error);
    return 0;
}
