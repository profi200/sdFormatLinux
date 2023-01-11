#include <cstdio>
#include <cstring>
#include <sys/random.h> // getrandom()...
#include "types.h"
#include "mbr.h"
#include "format.h"
#include "verbose_printf.h"



// Converts LBA to MBR CHS format.
// TODO: SDFormatter uses end head 254 for a 16 GB card. Bug?
static u32 lba2chs(u64 lba, const u32 heads, const u32 secPerTrk)
{
	// We can't encode this even with 255 heads and 63 sectors per track.
	// Returning the maximum in this case is ok.
	if(lba >= 16450560)
		return 0x00FFFFFF; // Cylinder 1023, head 255, sector 63.

	const u32 spc = heads * secPerTrk;
	const u32 c = lba / spc;
	lba %= spc;
	const u32 h = lba / secPerTrk;
	const u32 s = lba % secPerTrk + 1;

	// Return the maximum if we can't encode this
	// and also set the uppermost byte to maximum to indicate an error.
	if(c > 1023 || h > 255 || s > 63)
		return 0xFFFFFFFF;

	//printf("lba2chs(%" PRIu64 ", %" PRIu32 ", %" PRIu32 "): c: %" PRIu32 ", h: %" PRIu32 ", s: %" PRIu32 "\n",
	//       lba, heads, secPerTrk, c, h, s);
	return (c & 0xFF)<<16 | (c & 0x300)<<6 | s<<8 | h;
}

// TODO: Rewrite this function to use partSize to determine the fs type.
int createMbrAndPartition(const FormatParams *const params, BufferedFsWriter &dev)
{
	// Master Boot Record (MBR).
	// Generate a new, random disk signature.
	// TODO: If getrandom() returns -1 abort. We should probably make a wrapper.
	Mbr mbr{};
	while(getrandom(&mbr.diskSig, 4, 0) != 4);
	verbosePrintf("Disk ID: 0x%08" PRIX32 "\n", mbr.diskSig);

	// Set partition to inactive.
	PartEntry &entry = mbr.partTable[0];
	entry.status = 0x00;

	// Set start C/H/S.
	const u32 partStart = params->partStart;
	const u32 heads     = params->heads;
	const u32 secPerTrk = params->secPerTrk;
	const u32 startCHS  = lba2chs(partStart, heads, secPerTrk);
	memcpy(entry.startCHS, &startCHS, 3);

	// Set partition filesystem type.
	const u8 fatBits   = params->fatBits;
	const u64 totSec   = params->totSec;
	const u64 partSize = totSec - partStart; // TODO: Is this correct or should we align the end?
	u8 type;
	if(fatBits == 12)               type = 0x01; // FAT12 (16/32 MiB).
	else if(fatBits == 16)
	{
		if(partSize < 65536)        type = 0x04; // FAT16 (<32 MiB). TODO: Is this actually "<" or "<="?
		else                        type = 0x06; // FAT16 (>=32 MiB).
	}
	else if(fatBits == 32)
	{
		if((totSec - 1) < 16450560) type = 0x0B; // FAT32 CHS.
		else                        type = 0x0C; // FAT32 LBA.
	}
	else                            type = 0x07; // exFAT.
	entry.type = type;
	verbosePrintf("Partition type: 0x%02" PRIX8 "\n", type);

	// Set end C/H/S.
	const u32 endCHS = lba2chs(totSec - 1, heads, secPerTrk);
	memcpy(entry.endCHS, &endCHS, 3);

	// Set start LBA and number of sectors.
	entry.startLBA = partStart;
	entry.sectors  = (u32)partSize;

	// Set boot signature.
	mbr.bootSig = 0xAA55;

	// Write new MBR to card.
	return dev.write(reinterpret_cast<u8*>(&mbr), sizeof(Mbr));
}
