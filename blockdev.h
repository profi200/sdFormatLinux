#pragma once

#include "types.h"



int blkdevOpen(const char *const path);
u64 blkdevGetSectors(void);
int blkdevReadSectors(void *buf, const u64 sector, const u64 count);
int blkdevWriteSectors(const void *buf, const u64 sector, const u64 count);
int blkdevTruncate(const u64 sectors);
void blkdevClose(void);
