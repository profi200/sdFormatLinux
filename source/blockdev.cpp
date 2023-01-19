#define _FILE_OFFSET_BITS 64
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <fcntl.h>     // open()...
#include <linux/fs.h>  // BLKGETSIZE64...
#include <sys/ioctl.h> // ioctl()...
#include <sys/stat.h>  // S_IRUSR, S_IWUSR...
#include <unistd.h>    // write(), close()...
#include "types.h"
#include "blockdev.h"


//#define REDIRECT_FOR_DEBUG (1)
#define MAX_TOKENS         (4)



// Assumes at least 1 token.
static u32 tokenize(char *const line, const char *tokens[MAX_TOKENS])
{
	memset(tokens, 0, sizeof(char*) * MAX_TOKENS);
	tokens[0] = strtok(line, " ,\t\n");
	u32 num = 1;
	for(u32 i = 1; i < MAX_TOKENS; i++)
	{
		if((tokens[i] = strtok(nullptr, " ,\t\n")) == nullptr) break;
		num++;
	}

	return num;
}

static int checkDevice(const char *const path)
{
	int res = EINVAL; // By default assume the given path is not a suitable device.
	char cmd[64] = "lsblk -nr -oTYPE,HOTPLUG,PHY-SEC,MOUNTPOINT ";
	strncpy(&cmd[44], path, sizeof(cmd) - 44);
	cmd[sizeof(cmd) - 1] = '\0';
	FILE *const p = ::popen(cmd, "r");
	if(p == nullptr)
	{
		res = errno;
		perror("Failed to call lsblk");
		return res;
	}

	char line[64];
	line[sizeof(line) - 1] = '\0';
	while(fgets(line, sizeof(line), p) != nullptr) // TODO: We should probably check for errors with ferror().
	{
		const char *tokens[MAX_TOKENS];
		tokenize(line, tokens);

		// Check for disk, hotplug and sector size or alternatively for a loop device.
		// This assumes that lsblk never outputs disk or loop entries
		// when given the path to a partition or file.
		if((strcmp(tokens[0], "disk") == 0 && *tokens[1] == '1' && strcmp(tokens[2], "512") == 0) ||
		   strcmp(tokens[0], "loop") == 0)
			res = 0;

		// If any of the partitions is mounted stop here.
		if(tokens[3] != nullptr)
		{
			res = EACCES;
			break;
		}
	}

	const int pres = ::pclose(p);
	if(pres == -1)     res = errno;  // pclose() error.
	else if(pres != 0) res = EINVAL; // lsblk error.

	return res;
}

int BlockDev::open(const char *const path, const bool rw) noexcept
{
	int res = 0;
	int fd = -1;
	do
	{
		res = checkDevice(path);
		errno = res; // For perror() at the end.
		if(res == EACCES)
		{
			fputs("Device is a file or partitions are mounted.\n", stderr);
			break;
		}
		else if(res != 0)
			break;

		// Note: There is no reliable way of locking block device files so we don't and hope nothing explodes.
		//       flock() only works between processes using it.
		fd = ::open(path, (rw ? O_RDWR : O_RDONLY));
		if(fd == -1)
		{
			res = errno;
			break;
		}

		u64 diskSize;
		if(ioctl(fd, BLKGETSIZE64, &diskSize) == -1)
		{
			res = errno;
			break;
		}

#ifdef REDIRECT_FOR_DEBUG
		while(::close(fd) == -1 && errno == EINTR);

		// Create file with -rw-rw-rw- permissions.
		fd = ::open("sdFormatLinux_dump.bin", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		if(fd == -1)
		{
			res = errno;
			break;
		}
#endif

		m_fd = fd;
		m_sectors = diskSize / m_sectorSize;
	} while(0);

	if(res != 0)
	{
		perror("Failed to open block device");
		if(fd != -1) ::close(fd);
	}
	return res;
}

int BlockDev::read(u8 *buf, const u64 sector, const u64 count) const noexcept
{
	int res = 0;
	const int fd = m_fd;
	off_t offset = sector * m_sectorSize;
	u64 totSize = count * m_sectorSize;
	while(totSize > 0)
	{
		// Limit of 1 GiB chunks.
		const size_t blkSize = (totSize > 0x40000000 ? 0x40000000 : totSize);
		const ssize_t _read = ::pread(fd, buf, blkSize, offset);
		if(_read == -1)
		{
			res = errno;
			break;
		}

		buf += _read;
		offset += _read;
		totSize -= _read;
	}

	if(res != 0) perror("Failed to read from block device");
	return res;
}

int BlockDev::write(const u8 *buf, const u64 sector, const u64 count) noexcept
{
#ifdef REDIRECT_FOR_DEBUG
	// Limit to 1 GiB in case we screw up in debug mode.
	if(sector > ~count || sector + count > 0x40000000) return EINVAL;
#endif

	// Mark as dirty since we are about to change data.
	m_dirty = true;

	int res = 0;
	const int fd = m_fd;
	off_t offset = sector * m_sectorSize;
	u64 totSize = count * m_sectorSize;
	while(totSize > 0)
	{
		// Limit of 1 GiB chunks.
		const size_t blkSize = (totSize > 0x40000000 ? 0x40000000 : totSize);
		const ssize_t written = ::pwrite(fd, buf, blkSize, offset);
		if(written == -1)
		{
			res = errno;
			break;
		}

		buf += written;
		offset += written;
		totSize -= written;
	}

	if(res != 0) perror("Failed to write to block device");
	return res;

	return 0;
}

int BlockDev::eraseAll(const bool secure) const noexcept
{
	int res = 0;
	const u64 wholeRange[2] = {0, m_sectors * m_sectorSize};
	if(ioctl(m_fd, (secure ? BLKSECDISCARD : BLKDISCARD), wholeRange) == -1)
	{
		res = errno;
		perror("Failed to discard all data on device");
	}

	return res;
}

// TODO: Should we return any error that is not EINTR?
void BlockDev::close(void) noexcept
{
	const int fd = m_fd;
	if(m_dirty)
	{
		// Flush all writes to the device.
		fsync(fd);

		// Force partition rescanning so the kernel can see the changes.
		ioctl(fd, BLKRRPART);
	}

	// Close the file descriptor.
	while(::close(fd) == -1 && errno == EINTR);

	m_dirty = false;
	m_fd = -1;
	m_sectors = 0;
}
