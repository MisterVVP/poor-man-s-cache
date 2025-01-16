#pragma once
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <cmath>
#include <queue>

class KeyValueStore {
    private:
        static const unsigned int DOUBLE_HASHING_THRESHOLD = 500000;
        static const uint_fast8_t MAX_READ_WRITE_ATTEMPTS = 5;
        static const uint_fast8_t BUCKET_SIZE = 4;

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

        std::queue<uint_fast64_t> primeQueue;

        uint_fast64_t calcIndex(uint_fast64_t hash, int attempt, uint_fast64_t tableSize) const;
        uint_fast64_t hash(const char *key) const;

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

        uint_fast64_t getNumResizes() {
            return numResizes;
        }

        bool set(const char *key, const char *value);
        const char *get(const char *key);
};