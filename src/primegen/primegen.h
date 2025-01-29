#pragma once
#include <iostream>
#include <vector>
#include <queue>
#include <bitset>
#include <cmath>
#include <limits>
#include <omp.h>

constexpr uint_fast64_t DEFAULT_MAX_LIMIT = 1000000000; // 1 billion is reasonable default limit
constexpr uint_fast64_t SEGMENT_SIZE = 1000000;

class Primegen {
private:
    std::queue<uint_fast64_t> primeQueue;
    uint_fast64_t maxLimit;

    void generatePrimeQueue();

public:
    Primegen(uint_fast64_t maxLimit = DEFAULT_MAX_LIMIT);
    uint_fast64_t PopNext();
};