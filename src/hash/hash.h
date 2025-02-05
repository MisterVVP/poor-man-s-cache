#pragma once
#include <cstdint>
#include <cstring>
#include "MurmurHash3.h"

uint_fast64_t hashFunc(const char *key);