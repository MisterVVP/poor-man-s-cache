#pragma once
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <cmath>
#include <queue>

#define BUCKET_SIZE 4
#define MAX_READ_WRITE_ATTEMPTS 5
#define RESIZE_THRESHOLD_PERCENTAGE 70

struct KeyValueStoreSettings {
    uint_fast64_t initialSize = 2053;
    bool encodingEnabled = false; //TODO : Delta encoding does not really compress data a lot, so encoding is better to be disabled by default
};

class KeyValueStore {
    private:
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
        bool encodingEnabled;
        uint_fast32_t numResizes;

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
        KeyValueStore(KeyValueStoreSettings settings = KeyValueStoreSettings{});
        ~KeyValueStore();

        uint_fast64_t getTableSize() {
            return tableSize;
        }

        uint_fast64_t getNumEntries() {
            return numEntries;
        }

        uint_fast32_t getNumResizes() {
            return numResizes;
        }

        bool set(const char *key, const char *value);
        const char *get(const char *key);
};