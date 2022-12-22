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


// Create files with rw-rw---- permissions.
#define DEFAULT_PERM  (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)



int BlockDev::open(const char *const path, const bool rw) noexcept
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
		fd = ::open(path, (rw ? O_RDWR : O_RDONLY) | O_CREAT, DEFAULT_PERM);
		if(fd == -1)
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
			m_isRegularFile = true;
			diskSize = fileStat.st_size;
		}
		else
		{
			if(ioctl(fd, BLKGETSIZE64, &diskSize) == -1)
			{
				res = errno;
				break;
			}
		}

		m_fd = fd;
		m_diskSectors = diskSize / 512;
	} while(0);

	if(res != 0)
	{
		perror("Failed to open block device");
		if(fd != -1) ::close(fd);
	}
	return res;
}

// TODO: Use pread()?
int BlockDev::read(u8 *buf, const u64 sector, const u64 count) const noexcept
{
	int res = 0;
	const int fd = m_fd;
	if(lseek(fd, sector * 512, SEEK_SET) != -1)
	{
		u64 totSize = count * 512;
		while(totSize > 0)
		{
			// Limit of 1 GiB chunks.
			const size_t blkSize = (totSize > 0x40000000 ? 0x40000000 : totSize);
			const ssize_t _read = ::read(fd, buf, blkSize);
			if(_read == -1)
			{
				res = errno;
				break;;
			}

			buf += _read;
			totSize -= _read;
		}
	}
	else res = errno;

	if(res != 0) perror("Failed to read from block device");
	return res;
}

// TODO: Use pwrite()?
int BlockDev::write(const u8 *buf, const u64 sector, const u64 count) noexcept
{
	int res = 0;
	const int fd = m_fd;
	if(lseek(fd, sector * 512, SEEK_SET) != -1)
	{
		m_isDevDirty = true;
		u64 totSize = count * 512;
		while(totSize > 0)
		{
			// Limit of 1 GiB chunks.
			const size_t blkSize = (totSize > 0x40000000 ? 0x40000000 : totSize);
			const ssize_t written = ::write(fd, buf, blkSize);
			if(written == -1)
			{
				res = errno;
				break;;
			}

			buf += written;
			totSize -= written;
		}
	}
	else res = errno;

	if(res != 0) perror("Failed to write to block device");
	return res;
}

// Note: This is only valid for image files.
int BlockDev::truncate(const u64 sectors) noexcept
{
	int res = 0;
	const int fd = m_fd;
	if(m_isRegularFile == true)
	{
		while((res = ftruncate(fd, 512 * sectors)) == -1 && errno == EINTR);
	}
	else res = ENOTBLK;

	if(res == 0) m_diskSectors = sectors;
	else if(res != 0) perror("Failed to truncate file");
	return res;
}

int BlockDev::discardAll(const bool secure) const noexcept
{
	int res = 0;
	if(!m_isRegularFile)
	{
		const u64 wholeDevRange[2] = {0, m_diskSectors * 512};
		if(ioctl(m_fd, (secure ? BLKSECDISCARD : BLKDISCARD), wholeDevRange) == -1)
			res = errno;
	}
	else
	{
		res = EOPNOTSUPP;
		errno = EOPNOTSUPP;
	}

	if(res != 0) perror("Failed to discard all data on device");
	return res;
}

// TODO: Should we return any error that is not EINTR?
void BlockDev::close(void) noexcept
{
	// Make sure all writes are flushed to the device.
	const int fd = m_fd;
	fsync(fd);

	// Force partition rescanning if we wrote any data.
	if(m_isDevDirty) ioctl(fd, BLKRRPART);

	// Close the file descriptor.
	while(::close(fd) == -1 && errno == EINTR);

	m_isRegularFile = false;
	m_isDevDirty = false;
	m_fd = -1;
	m_diskSectors = 0;
}
