#define _FILE_OFFSET_BITS 64
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>     // open()...
#include <linux/fs.h>  // BLKGETSIZE64...
#include <sys/ioctl.h> // ioctl()...
#include <sys/stat.h>  // S_IRUSR, S_IWUSR...
#include <unistd.h>    // write(), close()...
#include "types.h"


// Create files with rw-rw---- permissions.
#define DEFAULT_PERM  (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)


static int g_fd = -1;
static bool g_isRegularFile = false;
static u64 g_diskSectors = 0;



int blkdevOpen(const char *const path)
{
	// Always block access to /dev/sdaX because in 99% of cases this is the system drive.
	// TODO: There may be edge cases where this is not true.
	//       Find a more reliable way to detect system drives.
	//       This won't work for example on devices with NVMe SSD.
	if(strncmp(path, "/dev/sda", 8) == 0)
	{
		fprintf(stderr, "Blocked access to '%s'. Don't mess with your system drive!\n", path);
		return EACCES;
	}

	int res = 0;
	int fd;
	do
	{
		// Note: There is no reliable way of locking block device files so we don't and hope nothing explodes.
		//       flock() only works between processes using it.
		fd = open(path, O_RDWR | O_CREAT, DEFAULT_PERM);
		if(fd == (-1))
		{
			res = errno;
			break;
		}

		u64 diskSize;
		struct stat fileStat;
		if(fstat(fd, &fileStat) != 0)
		{
			res = errno;
			break;
		}

		// Test if regular file or block device.
		if(!S_ISBLK(fileStat.st_mode))
		{
			g_isRegularFile = true;
			diskSize = fileStat.st_size;
		}
		else
		{
			if(ioctl(fd, BLKGETSIZE64, &diskSize) != 0)
			{
				res = errno;
				break;
			}
		}

		g_fd = fd;
		g_diskSectors = diskSize / 512;
	} while(0);

	if(res != 0)
	{
		perror("Failed to open block device");
		if(fd != (-1)) close(fd);
	}
	return res;
}

u64 blkdevGetSectors(void)
{
	return g_diskSectors;
}

int blkdevReadSectors(void *buf, const u64 sector, const u64 count)
{
	int res = 0;
	const int fd = g_fd;
	if(lseek(fd, sector * 512, SEEK_SET) != (-1))
	{
		u64 totSize = count * 512;
		while(totSize > 0)
		{
			// Limit of 1 GiB chunks.
			const size_t blkSize = (totSize > 0x40000000 ? 0x40000000 : totSize);
			const ssize_t _read = read(fd, buf, blkSize);
			if(_read == (-1))
			{
				res = errno;
				break;;
			}

			buf += _read; // TODO: void pointer math is ub.
			totSize -= _read;
		}
	}
	else res = errno;

	if(res != 0) perror("Failed to read from block device");
	return res;
}

int blkdevWriteSectors(const void *buf, const u64 sector, const u64 count)
{
	int res = 0;
	const int fd = g_fd;
	if(lseek(fd, sector * 512, SEEK_SET) != (-1))
	{
		u64 totSize = count * 512;
		while(totSize > 0)
		{
			// Limit of 1 GiB chunks.
			const size_t blkSize = (totSize > 0x40000000 ? 0x40000000 : totSize);
			const ssize_t written = write(fd, buf, blkSize);
			if(written == (-1))
			{
				res = errno;
				break;;
			}

			buf += written; // TODO: void pointer math is ub.
			totSize -= written;
		}
	}
	else res = errno;

	if(res != 0) perror("Failed to write to block device");
	return res;
}

// Note: This is only valid for image files.
int blkdevTruncate(const u64 sectors)
{
	int res = 0;
	const int fd = g_fd;
	if(g_isRegularFile == true)
	{
		while((res = ftruncate(fd, 512 * sectors)) == (-1) && errno == EINTR);
	}
	else res = ENOTBLK;

	if(res == 0) g_diskSectors = sectors;
	else if(res != 0) perror("Failed to truncate file");
	return res;
}

// TODO: Should we return any error that is not EINTR?
void blkdevClose(void)
{
	const int fd = g_fd;
	fsync(fd);
	while(close(fd) == (-1) && errno == EINTR);

	g_fd = -1;
	g_isRegularFile = false;
	g_diskSectors = 0;
}
