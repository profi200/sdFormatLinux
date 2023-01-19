#pragma once

#include <cstddef>
#include "types.h"
#include "format.h"
#include "buffered_fs_writer.h"


typedef struct
{
	u8 status; // 0x80 active/bootable, 0x00 inactive.
	u8 startCHS[3];
	u8 type;
	u8 endCHS[3];
	u32 startLBA;
	u32 sectors;
} __attribute__((packed)) PartEntry;
static_assert(offsetof(PartEntry, sectors) == 12, "Member sectors of PartEntry not at offsetof 12.");

typedef struct
{
	u8 bootstrap[440];
	u32 diskSig;
	u16 reserved; // 0x0000. 0x5A5A for copy protected.
	PartEntry partTable[4];
	u16 bootSig;
} __attribute__((packed)) Mbr;
static_assert(offsetof(Mbr, bootSig) == 510, "Member bootSig of Mbr not at offsetof 510.");



int createMbrAndPartition(const FormatParams &params, BufferedFsWriter &dev);
