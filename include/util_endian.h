#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2023 profi200

#include <bit>
#include <concepts>
#include "types.h"



namespace util::endian
{

consteval bool cpuBe(void)
{
	return std::endian::native == std::endian::big;
}

consteval bool cpuLe(void)
{
	return std::endian::native == std::endian::little;
}

template <std::integral T>
constexpr static inline T bswap(const T val) noexcept
{
	if constexpr(sizeof(T) == sizeof(u64))
		return __builtin_bswap64(val);
	else if constexpr(sizeof(T) == sizeof(u32))
		return __builtin_bswap32(val);
	else
	{
		static_assert(sizeof(T) == sizeof(u16), "Type not supported for util::endian::bswap().");
		return __builtin_bswap16(val);
	}
}
static_assert(bswap((u16)0x0201) == 0x0102, "16 bit endian swap is broken.");
static_assert(bswap((u32)0x04030201) == 0x01020304, "32 bit endian swap is broken.");
static_assert(bswap((u64)0x0807060504030201) == 0x0102030405060708, "64 bit endian swap is broken.");

template <std::integral T>
constexpr static inline T cpuToBe(const T val) noexcept
{
	if constexpr(cpuBe())
		return val;
	else return bswap(val);
}

template <std::integral T>
constexpr static inline T cpuToLe(const T val) noexcept
{
	if constexpr(cpuLe())
		return val;
	else return bswap(val);
}

template <std::integral T>
constexpr static inline T beToCpu(const T val) noexcept
{
	return cpuToBe(val);
}

template <std::integral T>
constexpr static inline T leToCpu(const T val) noexcept
{
	return cpuToLe(val);
}

} // namespace util


template <std::integral T>
struct BigEndianIntegral final
{
	//constexpr BigEndianIntegral(void) noexcept : m_be(0) {}
	constexpr BigEndianIntegral(const T &val) noexcept : m_be(util::endian::cpuToBe(val)) {}
	constexpr operator T(void) const noexcept {return util::endian::beToCpu(m_be);}

private:
	T m_be;
};

template <std::integral T>
struct LittleEndianIntegral final
{
	//constexpr LittleEndianIntegral(void) noexcept : m_le(0) {}
	constexpr LittleEndianIntegral(const T &val) noexcept : m_le(util::endian::cpuToLe(val)) {}
	constexpr operator T(void) const noexcept {return util::endian::leToCpu(m_le);}

private:
	T m_le;
};

using u16be = BigEndianIntegral<u16>;
using u32be = BigEndianIntegral<u32>;
using u64be = BigEndianIntegral<u64>;
using u16le = LittleEndianIntegral<u16>;
using u32le = LittleEndianIntegral<u32>;
using u64le = LittleEndianIntegral<u64>;
static_assert(sizeof(u16be) == 2 && alignof(u16be) == 2, "u16be is not 2 bytes or not aligned to 2 bytes.");
static_assert(sizeof(u32be) == 4 && alignof(u32be) == 4, "u32be is not 4 bytes or not aligned to 4 bytes.");
static_assert(sizeof(u64be) == 8 && alignof(u64be) == 8, "u64be is not 8 bytes or not aligned to 8 bytes.");
static_assert(sizeof(u16le) == 2 && alignof(u16le) == 2, "u16le is not 2 bytes or not aligned to 2 bytes.");
static_assert(sizeof(u32le) == 4 && alignof(u32le) == 4, "u32le is not 4 bytes or not aligned to 4 bytes.");
static_assert(sizeof(u64le) == 8 && alignof(u64le) == 8, "u64le is not 8 bytes or not aligned to 8 bytes.");

using s16be = BigEndianIntegral<s16>;
using s32be = BigEndianIntegral<s32>;
using s64be = BigEndianIntegral<s64>;
using s16le = LittleEndianIntegral<s16>;
using s32le = LittleEndianIntegral<s32>;
using s64le = LittleEndianIntegral<s64>;
static_assert(sizeof(s16be) == 2 && alignof(s16be) == 2, "s16be is not 2 bytes or not aligned to 2 bytes.");
static_assert(sizeof(s32be) == 4 && alignof(s32be) == 4, "s32be is not 4 bytes or not aligned to 4 bytes.");
static_assert(sizeof(s64be) == 8 && alignof(s64be) == 8, "s64be is not 8 bytes or not aligned to 8 bytes.");
static_assert(sizeof(s16le) == 2 && alignof(s16le) == 2, "s16le is not 2 bytes or not aligned to 2 bytes.");
static_assert(sizeof(s32le) == 4 && alignof(s32le) == 4, "s32le is not 4 bytes or not aligned to 4 bytes.");
static_assert(sizeof(s64le) == 8 && alignof(s64le) == 8, "s64le is not 8 bytes or not aligned to 8 bytes.");
