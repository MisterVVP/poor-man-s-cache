#pragma once
#include <time.h>

// Constant for nanoseconds per second.
constexpr long NANOSECONDS_IN_SECOND = 1000000000L;

/// @brief Overload operator+ to add two timespec structures.
/// @param lhs left
/// @param rhs right
/// @return sum of left and right
timespec operator+(const timespec &lhs, const timespec &rhs);

/// @brief Overload operator- to subtract one timespec from another.
/// @param lhs left
/// @param rhs right
/// @return difference of left and right
timespec operator-(const timespec &lhs, const timespec &rhs);

/// @brief Resets timespec values to 0
/// @param ts timespec struct
inline void resetTimespec(timespec &ts) {
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
};