#pragma once
#include <cstdint>
#include <cstring>
#include <string.h>
#include <charconv>
#include <iostream>
#include <cmath>
#include <queue>
#include <algorithm>

#ifndef NDEBUG
#include <chrono>
#endif

#define UNIT_SEPARATOR 0x1F
#define BUCKET_SIZE 4
#define MAX_READ_WRITE_ATTEMPTS 5
#define RESIZE_THRESHOLD_PERCENTAGE 70
#define FREQ_DICT_SIZE 512
#define COMPRESSION_FREQUENCY 3


struct KeyValueStoreSettings {
    uint_fast64_t initialSize = 2053;
    bool compressionEnabled = true;
    bool usePrimeNumbers = true;
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
    uint_fast32_t numResizes;


    bool isResizing;
    void resize();
    uint_fast64_t calcIndex(uint_fast64_t hash, int attempt, uint_fast64_t tableSize) const;
    uint_fast64_t hash(const char *key) const;
    void cleanTable(Bucket* tableToDelete, uint_fast64_t size);

    
    bool usePrimeNumbers;
    std::queue<uint_fast64_t> primeQueue;
    bool isPrime(uint_fast64_t n) const;
    uint_fast64_t nextPrime(uint_fast64_t start) const;
    void generatePrimeQueue();


    struct SubstringFrequency {
        const char* substring;
        size_t count;
    };
    bool compressionEnabled;
    KeyValueStore* compressDictionary;
    void rebuildCompressionDictionary();
    char* compress(const char* value);
    char* decompress(const char* compressedValue);

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
