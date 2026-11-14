#ifndef CTCA_INTERNAL_H
#define CTCA_INTERNAL_H

/* Internal adapter hooks. Not part of the legacy public API. */
int ctca_sync_area(int areaid);
/* Drop only adapter requests still queued at the Coupler, never dispatched work. */
int ctca_drop_queued(int connection, int source_rank);
/* Optional high-level diagnostics; legacy callers keep an unset guard. */
void ctca_set_wait_guard(void (*guard)(const char *, double));

#endif
