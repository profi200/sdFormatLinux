// SPDX-License-Identifier: MIT
// Copyright (c) 2023 profi200

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



static int checkDevice(const char *const path)
{
	int res = EINVAL; // By default assume the given path is not a suitable device.
	char cmd[64] = "/usr/bin/lsblk -dnr -oTYPE,HOTPLUG,PHY-SEC ";
	strncpy(&cmd[43], path, sizeof(cmd) - 43);
	cmd[sizeof(cmd) - 1] = '\0';
	FILE *const p = ::popen(cmd, "r");
	if(p == nullptr)
	{
		res = errno;
		perror("Failed to call lsblk");
		return res;
	}

	char line[16];
	line[sizeof(line) - 1] = '\0';
	if(fgets(line, sizeof(line), p) == nullptr)
	{
		::pclose(p);
		return res;
	}

	if(strcmp(line, "disk 1 512\n") == 0 || strcmp(line, "loop 0 512\n") == 0)
		res = 0;

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
		if(res == EINVAL)
		{
			fputs("Error: Not a suitable block device.\n", stderr);
			break;
		}
		else if(res != 0)
			break;

		// Note: There is no reliable way of locking block device files so we don't and hope nothing explodes.
		//       flock() only works between processes using it.
		// Under Linux opening a mounted block device with O_EXCL will fail with EBUSY.
		fd = ::open(path, (rw ? O_RDWR : O_RDONLY) | O_EXCL);
		if(fd == -1)
		{
			res = errno;
			if(res == EBUSY)
			{
				fputs("Error: Device is mounted.\n", stderr);
			}
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
		fd = ::open("./sdFormatLinux_dump.bin", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
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

int BlockDev::read(void *buf, const u64 sector, const u64 count) const noexcept
{
	int res = 0;
	const int fd = m_fd;
	off_t offset = sector * m_sectorSize;
	u64 totSize = count * m_sectorSize;
	u8 *_buf = reinterpret_cast<u8*>(buf);
	while(totSize > 0)
	{
		// Limit of 1 GiB chunks.
		const size_t blkSize = (totSize > 0x40000000 ? 0x40000000 : totSize);
		const ssize_t _read = ::pread(fd, _buf, blkSize, offset);
		if(_read == -1)
		{
			res = errno;
			break;
		}

		_buf += _read;
		offset += _read;
		totSize -= _read;
	}

	if(res != 0) perror("Failed to read from block device");
	return res;
}

int BlockDev::write(const void *buf, const u64 sector, const u64 count) noexcept
{
#ifdef REDIRECT_FOR_DEBUG
	// Limit to 1 GiB in case we screw up in debug mode.
	if(sector > ~count || sector + count > 0x40000000) return EINVAL;
#endif

	// Mark as dirty since we are about to write data.
	m_dirty = true;

	int res = 0;
	const int fd = m_fd;
	off_t offset = sector * m_sectorSize;
	u64 totSize = count * m_sectorSize;
	const u8 *_buf = reinterpret_cast<const u8*>(buf);
	while(totSize > 0)
	{
		// Limit of 1 GiB chunks.
		const size_t blkSize = (totSize > 0x40000000 ? 0x40000000 : totSize);
		const ssize_t written = ::pwrite(fd, _buf, blkSize, offset);
		if(written == -1)
		{
			res = errno;
			break;
		}

		_buf += written;
		offset += written;
		totSize -= written;
	}

	if(res != 0) perror("Failed to write to block device");
	return res;
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
