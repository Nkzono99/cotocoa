#ifndef CTCA_INTERNAL_H
#define CTCA_INTERNAL_H
#include <stddef.h>

/* Internal adapter hooks. Not part of the legacy public API. */
int ctca_sync_area(int areaid);
/* Drop only adapter requests still queued at the Coupler, never dispatched work. */
int ctca_drop_queued(int connection, int source_rank);
/* Optional high-level diagnostics; legacy callers keep an unset guard. */
void ctca_set_wait_guard(void (*guard)(const char *, double));
/* Let MPI allocate the request-status table and the areas registered with
 * ctca_regarea_allocate (MPI_Win_allocate instead of MPI_Win_create). A peer can
 * then read and write them while the owning rank is computing outside MPI, where
 * the MPI library supports that (shared memory, RDMA). Every rank of the job must
 * choose the same setting before its init call; legacy programs leave it off. */
void ctca_set_window_allocation(int enabled);
/* Requester side of an MPI-allocated area: unit is the element size in bytes
 * (4 with integer_type selecting int32 or float32, or 8 for float64). */
int ctca_regarea_allocate(size_t size, int unit, int integer_type, void **base, int *areaid);

#endif
