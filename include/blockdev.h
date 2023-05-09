#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2023 profi200

#include "types.h"



class BlockDev
{
	static constexpr u32 m_sectorSize = 512;
	bool m_dirty;
	int m_fd;
	u64 m_sectors;


	BlockDev(const BlockDev&) noexcept = delete; // Copy
	BlockDev(BlockDev&&) noexcept = delete;      // Move

	BlockDev& operator =(const BlockDev&) noexcept = delete; // Copy
	BlockDev& operator =(BlockDev&&) noexcept = delete;      // Move


public:
	BlockDev(void) noexcept : m_dirty(false), m_fd(-1), m_sectors(0) {}
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
	 * @brief      Returns the sector size in bytes.
	 *
	 * @return     The sector size.
	 */
	static constexpr u32 getSectorSize(void) {return m_sectorSize;}

	/**
	 * @brief      Returns the number of sectors.
	 *
	 * @return     The number of sectors.
	 */
	u64 getSectors(void) const noexcept {return m_sectors;}

	/**
	 * @brief      Reads sectors from the block device.
	 *
	 * @param      buf     The output buffer.
	 * @param[in]  sector  The start sector.
	 * @param[in]  count   The number of sectors to read.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int read(void *buf, const u64 sector, const u64 count) const noexcept;

	/**
	 * @brief      Writes sectors to the block device.
	 *
	 * @param[in]  buf     The input buffer.
	 * @param[in]  sector  The start sector.
	 * @param[in]  count   The number of sectors to write.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int write(const void *buf, const u64 sector, const u64 count) noexcept;

	/**
	 * @brief      Perform a TRIM/erase on the whole block device.
	 *
	 * @param[in]  secure  If true do a secure erase. Currently unsupported by Linux.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int eraseAll(const bool secure = false) const noexcept;

	/**
	 * @brief      Closes the block device.
	 */
	void close(void) noexcept;
};
