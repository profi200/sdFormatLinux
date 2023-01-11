//#include <cstdio>
#include "buffered_fs_writer.h"



// TODO: Edge case testing.
int BufferedFsWriter::fill(const u64 offset) noexcept
{
//printf("BufferedFsWriter::fill(%lu) m_pos %lu\n", offset, m_pos);
	u64 pos = m_pos;
	if(pos == offset) return 0;
	if(offset < pos)  return EINVAL;

	// Align to buffer size.
	const u64 distance = offset - pos;
	const u32 misalignment = ((pos + m_blkMask) & ~((u64)m_blkMask)) - pos;
	if(misalignment > 0)
	{
		const u32 fillSize = distance < misalignment ? distance : misalignment;
		memset(&m_buf[pos & m_blkMask], 0, fillSize);
		if(fillSize == misalignment)
		{
			const int res = BlockDev::write(m_buf.get(), (pos & ~((u64)m_blkMask)) / 512, m_blkSize / 512);
			if(res != 0) return res;
		}
		pos += fillSize;
	}

	// Write full blocks.
	u64 blocksToWrite = (offset - pos) / m_blkSize;
	if(blocksToWrite > 0)
	{
		memset(m_buf.get(), 0, m_blkSize);
		do
		{
			const int res = BlockDev::write(m_buf.get(), pos / 512, m_blkSize / 512);
			if(res != 0) return res;

			pos += m_blkSize;
		} while(--blocksToWrite);
	}
	else
	{
		// Remaining bytes.
		memset(m_buf.get(), 0, offset - pos);
	}

	m_pos = offset;

	return 0;
}

// TODO: Edge case testing.
int BufferedFsWriter::write(const u8 *buf, const u64 size) noexcept
{
//printf("BufferedFsWriter::write(%p, %lu) m_pos %lu\n", buf, size, m_pos);
	u64 pos = m_pos;
	const u64 end = pos + size;
	if(size == 0) return 0;
	if(end < size) return EINVAL;

	// Align to buffer size.
	const u32 misalignment = ((pos + m_blkMask) & ~((u64)m_blkMask)) - pos;
	if(misalignment > 0)
	{
		const u32 copySize = size < misalignment ? size : misalignment;
		memcpy(&m_buf[pos & m_blkMask], buf, copySize);
		if(copySize == misalignment)
		{
			const int res = BlockDev::write(m_buf.get(), (pos & ~((u64)m_blkMask)) / 512, m_blkSize / 512);
			if(res != 0) return res;
		}
		buf += copySize;
		pos += copySize;
	}

	// Write full blocks.
	while(pos < (end & ~((u64)m_blkMask))) // TODO: Use this same calculation in seekAndFill()?
	{
		const int res = BlockDev::write(buf, pos / 512, m_blkSize / 512);
		if(res != 0) return res;

		buf += m_blkSize;
		pos += m_blkSize;
	}

	// Remaining bytes.
	memcpy(m_buf.get(), buf, end - pos);

	m_pos = end;

	return 0;
}

// TODO: Edge case testing.
int BufferedFsWriter::close(void) noexcept
{
//printf("BufferedFsWriter::close() m_pos %lu\n", m_pos);
	// Align to sector size.
	constexpr u32 secMask = BlockDev::getSectorSize() - 1;
	const u64 pos = m_pos;
	const u32 misalignment = ((pos + secMask) & ~((u64)secMask)) - pos;
	memset(&m_buf[pos & m_blkMask], 0, misalignment);

	const u64 wrCount = ((pos + misalignment) & m_blkMask) / 512;
	const u64 wrSector = (pos & ~((u64)m_blkMask)) / 512;
	int res = 0;
	if(wrCount > 0)
		res = BlockDev::write(m_buf.get(), wrSector, wrCount);

	BlockDev::close();
	m_pos = 0;

	return res;
}
