#ifndef CTCA_INTERNAL_H
#define CTCA_INTERNAL_H

/* Internal adapter hooks. Not part of the legacy public API. */
int ctca_sync_area(int areaid);
/* Drop only adapter requests still queued at the Coupler, never dispatched work. */
int ctca_drop_queued(int connection, int source_rank);

#endif
