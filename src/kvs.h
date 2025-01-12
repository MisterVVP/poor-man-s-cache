#pragma once
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <random>
#include "highwayhash/highwayhash_target.h"
#include "highwayhash/instruction_sets.h"


class KeyValueStore {
    private:
        struct Entry {
            char* key;
            char* value;
            bool occupied;
        };

        static const int_fast8_t HH_KEY_LEN = 4;
        static const int_fast8_t RESIZE_MULTIPLIER = 2;
        static const unsigned int DOUBLE_HASHING_THRESHOLD = 500000;
        static const int_fast8_t MAX_SCAN_ATTEMPTS = 5;


        Entry *table;
        uint_fast64_t tableSize;
        uint_fast64_t numEntries;
        bool isResizing;
        unsigned int numResizes;
        unsigned int numFullScans;
        uint64_t hhkey[HH_KEY_LEN];

        uint_fast64_t calcIndex(uint_fast64_t hash, int attempt, uint_fast64_t tableSize, uint_fast64_t hash2 = 0) const;
        uint_fast64_t hash(const char *key) const;
        uint_fast64_t hash2(const char *key) const;
        void resize();

    public:
        KeyValueStore(uint_fast64_t initialSize = 2048);
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