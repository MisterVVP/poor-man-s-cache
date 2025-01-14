#pragma once
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <random>
#include <queue>
#include "highwayhash/highwayhash_target.h"
#include "highwayhash/instruction_sets.h"

class KeyValueStore {
    private:
        static const int_fast8_t HH_KEY_LEN = 4;
        static const unsigned int DOUBLE_HASHING_THRESHOLD = 500000;
        static const int_fast8_t MAX_READ_WRITE_ATTEMPTS = 5;
        static const int_fast8_t BUCKET_SIZE = 4; // Number of entries per index
        const double_t RESIZE_MULTIPLIER = 1.5; // Resize multiplier

        struct Entry {
            char* key;
            char* value;
            bool occupied;
        };

        struct Bucket {
            Entry entries[BUCKET_SIZE];
        };

        Bucket *table;
        uint_fast64_t tableSize;
        uint_fast64_t numEntries;
        uint_fast64_t numCollisions;
        bool isResizing;

        uint_fast64_t numResizes;
        uint64_t hhkey[HH_KEY_LEN];

        std::queue<uint_fast64_t> primeQueue;

        uint_fast64_t calcIndex(uint_fast64_t hash, int attempt, uint_fast64_t tableSize, uint_fast64_t hash2 = 0) const;
        uint_fast64_t hash(const char *key) const;
        uint_fast64_t hash2(const char *key) const;
        void resize();
        bool isPrime(uint_fast64_t n) const;
        uint_fast64_t nextPrime(uint_fast64_t start) const;
        void generatePrimeQueue();

    protected:
        void cleanTable(Bucket* tableToDelete, uint_fast64_t size);

    public:
        KeyValueStore(uint_fast64_t initialSize = 2053);
        ~KeyValueStore();

        uint_fast64_t getTableSize() {
            return tableSize;
        }

        uint_fast64_t getNumEntries() {
            return numEntries;
        }

        bool set(const char *key, const char *value);
        const char *get(const char *key);
};