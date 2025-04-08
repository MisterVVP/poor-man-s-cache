#include "time.h"

timespec operator+(const timespec &lhs, const timespec &rhs) {
    timespec result;
    result.tv_sec = lhs.tv_sec + rhs.tv_sec;
    result.tv_nsec = lhs.tv_nsec + rhs.tv_nsec;
    // Normalize the result if nanoseconds overflow.
    if (result.tv_nsec >= NANOSECONDS_IN_SECOND) {
        result.tv_sec += result.tv_nsec / NANOSECONDS_IN_SECOND;
        result.tv_nsec %= NANOSECONDS_IN_SECOND;
    }
    return result;
};

timespec operator-(const timespec &lhs, const timespec &rhs) {
    timespec result;
    result.tv_sec = lhs.tv_sec - rhs.tv_sec;
    result.tv_nsec = lhs.tv_nsec - rhs.tv_nsec;
    // Normalize if nanoseconds underflow.
    if (result.tv_nsec < 0) {
        result.tv_sec -= 1;
        result.tv_nsec += NANOSECONDS_IN_SECOND;
    }
    return result;
};
