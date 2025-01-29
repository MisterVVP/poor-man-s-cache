#include "primegen.h"

void Primegen::generatePrimeQueue() {
    uint_fast64_t start = 2;
    uint_fast64_t lastStored = 2053;
    double growthFactor = 2.0;

    while (start < maxLimit) {
        uint_fast64_t end = std::min(start + SEGMENT_SIZE, maxLimit);
        std::bitset<SEGMENT_SIZE> isPrime;
        isPrime.set();

        #pragma omp parallel for schedule(dynamic)
        for (uint_fast64_t i = 2; i * i <= end; ++i) {
            if (i < start) continue;
            if (!isPrime[i - start]) continue;

            for (uint_fast64_t j = std::max(i * i, (start + i - 1) / i * i); j < end; j += i) {
                isPrime[j - start] = 0;
            }
        }

        for (uint_fast64_t i = 0; i < end - start; ++i) {
            if (isPrime[i]) {
                uint_fast64_t prime = start + i;

                uint_fast64_t nextCandidate = static_cast<uint_fast64_t>(lastStored * growthFactor);
                if (prime >= nextCandidate) {
                    primeQueue.push(prime);
                    lastStored = prime;

                    // Adjust growth factor dynamically
                    if (prime < 100000) {
                        growthFactor = 4;
                    } else if (prime < 1000000) {
                        growthFactor = 1.5;
                    } else if (prime < 10000000) {
                        growthFactor = 1.2;
                    } else if (prime < 100000000) {
                        growthFactor = 1.1;
                    } else {
                        growthFactor = 1.05;
                    }
                }
            }
        }

        start = end;  // Move to the next segment
    }
};

Primegen::Primegen(uint_fast64_t maxLimit): maxLimit(maxLimit) {
    generatePrimeQueue();
};

uint_fast64_t Primegen::PopNext() {
    if (primeQueue.empty()) {
        throw std::runtime_error("Prime number generator reached it's limit.");
    }
    auto next = primeQueue.front();
    primeQueue.pop();
    return next;
};
