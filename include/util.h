#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2023 profi200

#include <climits>
#include <concepts>


#define BIT(x)            (1u<<(x))
#define BITWIDTHOF(x)     (sizeof(x) * CHAR_BIT)
#define ARRAY_ENTRIES(x)  (sizeof(x) / sizeof(*(x)))



namespace util
{

// Only works for power of 2 alignment. If val is 0 the result is always 0.
template <std::unsigned_integral T, std::unsigned_integral T2>
constexpr static inline T alignUp(const T val, const T2 alignment) noexcept
{
	return ((val - 1) | static_cast<T>(alignment - 1)) + 1;
}

// Only works for power of 2 alignment. If val is 0 or <alignment the result is always 0.
template <std::unsigned_integral T, std::unsigned_integral T2>
constexpr static inline T alignDown(const T val, const T2 alignment) noexcept
{
	return val & ~static_cast<T>(alignment - 1);
}

// Round up to the next multiple. If val is 0 the result is always 0.
// val + multiple should not be bigger than maximum of T + 1.
template <std::unsigned_integral T, std::unsigned_integral T2>
constexpr static inline T roundUp(const T val, const T2 multiple) noexcept
{
	return ((val + (multiple - 1)) / multiple) * multiple;
}

// Safe count leading zeros.
template <std::unsigned_integral T>
constexpr static inline unsigned countLeadingZeros(const T val) noexcept
{
	if(val == static_cast<T>(0)) return BITWIDTHOF(T);

	if constexpr(sizeof(T) == sizeof(unsigned long long))
		return __builtin_clzll(val);
	else if constexpr(sizeof(T) == sizeof(unsigned long))
		return __builtin_clzl(val);
	else
	{
		static_assert(sizeof(T) <= sizeof(unsigned int), "Type too big for util::countLeadingZeros().");
		return __builtin_clz(val) - (BITWIDTHOF(unsigned int) - BITWIDTHOF(T));
	}
}

// Safe count trailing zeros.
template <std::unsigned_integral T>
constexpr static inline unsigned countTrailingZeros(const T val) noexcept
{
	if(val == static_cast<T>(0)) return BITWIDTHOF(T);

	if constexpr(sizeof(T) == sizeof(unsigned long long))
		return __builtin_ctzll(val);
	else if constexpr(sizeof(T) == sizeof(unsigned long))
		return __builtin_ctzl(val);
	else
	{
		static_assert(sizeof(T) <= sizeof(unsigned int), "Type too big for util::countTrailingZeros().");
		return __builtin_ctz(val);
	}
}

// Divide and round up to smallest integer not less than the result. dividend must be >0.
template <std::unsigned_integral T, std::unsigned_integral T2>
constexpr static inline T udivCeil(const T dividend, const T2 divider) noexcept
{
	//return (dividend + (divider - 1)) / divider; // dividend + (divider - 1) can overflow.
	return (dividend - 1) / divider + 1;
}

} // namespace util
