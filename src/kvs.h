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
        static const unsigned int HH_KEY_LEN = 4;
        static const unsigned int RESIZE_MULTIPLIER = 2;

        struct Entry {
            char* key;
            char* value;
            bool occupied;
        };

        Entry *table;
        unsigned long long tableSize;
        unsigned long long numEntries;
        bool isResizing;
        uint64_t hhkey[HH_KEY_LEN];

        unsigned long long hash(const char *key, int attempt) const;
        unsigned long long rehash(const char *key, int attempt, unsigned long long newTableSize) const;
        void resize();

    public:
        KeyValueStore(unsigned long long initialSize = 2048);
        ~KeyValueStore();

        unsigned long long getTableSize() {
            return tableSize;
        }

        unsigned long long getNumEntries() {
            return numEntries;
        }

        bool set(const char *key, const char *value);
        const char *get(const char *key);
};