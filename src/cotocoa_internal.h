#ifndef COTOCOA_INTERNAL_H
#define COTOCOA_INTERNAL_H
#include "cotocoa.h"
struct cotocoa_message {
    cotocoa_context *context;
    cotocoa_metadata metadata;
    void *data;
    int completed;
};
#endif
