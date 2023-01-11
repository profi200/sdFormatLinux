#pragma once

#include <cstring>
#include <memory>
#include <stdexcept>
#include "types.h"
#include "blockdev.h"



// Warning: This class is only suitable for overwriting like reformatting.
//          Padding for alignment is filled with zeros (no read-modify-write).
class BufferedFsWriter final : private BlockDev
{
	static constexpr u32 m_blkSize = 1024 * 1024 * 4; // Must be >=512 and power of 2.
	static constexpr u32 m_blkMask = m_blkSize - 1;
	static_assert(m_blkSize > 512 && (m_blkSize & m_blkMask) == 0, "Invalid buffer size for BufferedFsWriter.");

	const std::unique_ptr<u8[]> m_buf;
	u64 m_pos;


	BufferedFsWriter(const BufferedFsWriter&) noexcept = delete; // Copy
	BufferedFsWriter(BufferedFsWriter&&) noexcept = delete;      // Move

	BufferedFsWriter& operator =(const BufferedFsWriter&) noexcept = delete; // Copy
	BufferedFsWriter& operator =(BufferedFsWriter&&) noexcept = delete;      // Move


public:
	BufferedFsWriter(void) noexcept : m_buf(new(std::nothrow) u8[m_blkSize]), m_pos(0) {}
	~BufferedFsWriter(void) noexcept(false)
	{
		if(m_pos > 0)
		{
			// Don't make errors on flushing the buffer go unnoticed.
			if(close() != 0) throw std::runtime_error("Failed to flush buffer to device.");
		}
	}

	/**
	 * @brief      Opens the block device.
	 *
	 * @param[in]  path  The path.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int open(const char *const path) noexcept
	{
		if(!m_buf) return ENOMEM;
		return BlockDev::open(path, true);
	}

	/**
	 * @brief      Returns the number of sectors.
	 *
	 * @return     The number of sectors.
	 */
	u64 getSectors(void) const noexcept {return BlockDev::getSectors();}

	/**
	 * @brief      Returns the current write position/pointer.
	 *
	 * @return     The write position/pointer.
	 */
	u64 tell(void) const noexcept {return m_pos;}

	/**
	 * @brief      Seeks to offset and fills the distance with zeros.
	 *
	 * @param[in]  offset  The offset. Must not be lower than the current position.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int fill(const u64 offset) noexcept;

	/**
	 * @brief      Writes data.
	 *
	 * @param[in]  buf   The input buffer.
	 * @param[in]  size  The number of bytes to write.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int write(const u8 *buf, const u64 size) noexcept;

	/**
	 * @brief      Fills until offset and writes size bytes from buf.
	 *
	 * @param[in]  buf     The input buffer.
	 * @param[in]  offset  The offset. Must not be lower than the current position.
	 * @param[in]  size    The number of bytes to write.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int fillAndWrite(const u8 *buf, const u64 offset, const u64 size) noexcept
	{
		int res = fill(offset);
		if(res == 0) res = write(buf, size);
		return res;
	}

	/**
	 * @brief      Perform a TRIM/erase on the whole block device.
	 *
	 * @param[in]  secure  If true do a secure erase.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int discardAll(const bool secure = false) noexcept
	{
		memset(m_buf.get(), 0, m_pos & m_blkMask);
		m_pos = 0;
		return BlockDev::discardAll(secure);
	}

	/**
	 * @brief      Flushes the buffer and closes the block device.
	 *
	 * @return     Returns 0 on success or errno.
	 */
	int close(void) noexcept;
};