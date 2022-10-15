#pragma once

#include "types.h"



class BlockDev
{
	bool m_isRegularFile;
	bool m_isDevDirty;
	int m_fd;
	u64 m_diskSectors; // TODO: Store sector size along with this. ioctl(..., BLKSSZGET, ...). Or maybe BLKPBSZGET like dosfstools?


	BlockDev(const BlockDev&) noexcept = delete; // Copy
	BlockDev(BlockDev&&) noexcept = delete;      // Move

	BlockDev& operator =(const BlockDev&) noexcept = delete; // Copy
	BlockDev& operator =(BlockDev&&) noexcept = delete;      // Move


public:
	BlockDev(void) noexcept : m_isRegularFile(false), m_isDevDirty(false), m_fd(-1), m_diskSectors(0) {}
	~BlockDev(void) noexcept
	{
		if(m_fd != -1) close();
	}

	/**
	 * @brief      Opens the block device.
	 *
	 * @param[in]  path  The path.
	 * @param[in]  rw    When true open device in read + write mode.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int open(const char *const path, const bool rw = false) noexcept;

	/**
	 * @brief      Returns the number of sectors.
	 *
	 * @return     The number of sectors.
	 */
	u64 getSectors(void) const noexcept {return m_diskSectors;}

	/**
	 * @brief      Reads sectors from the block device.
	 *
	 * @param      buf     The output buffer.
	 * @param[in]  sector  The start sector.
	 * @param[in]  count   The number of sectors to read.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int read(u8 *buf, const u64 sector, const u64 count) const noexcept;

	/**
	 * @brief      Writes sectors to the block device.
	 *
	 * @param[in]  buf     The input buffer.
	 * @param[in]  sector  The start sector.
	 * @param[in]  count   The number of sectors to write.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int write(const u8 *buf, const u64 sector, const u64 count) noexcept;

	/**
	 * @brief      Truncates to the specified size if the device is a regular file. Otherwise not supported.
	 *
	 * @param[in]  sectors  The size in sectors.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int truncate(const u64 sectors) noexcept;

	/**
	 * @brief      Perform a TRIM/erase on the whole block device. Not supported for regular files.
	 *
	 * @param[in]  secure  If true do a secure erase.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int discardAll(const bool secure = false) const noexcept;

	/**
	 * @brief      Closes the block device.
	 */
	void close(void) noexcept;
};
