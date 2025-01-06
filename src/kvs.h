#pragma once
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <cmath>

// Custom key-value store implementation
class KeyValueStore {
    private:
        struct Entry {
            char key[64];
            char value[256];
            bool occupied;
        };

        Entry *table;
        int tableSize;
        int numEntries;

        int hash(const char *key, int attempt) const;
        void resize();

    public:
        KeyValueStore(int initialSize = 1024);
        ~KeyValueStore();

        bool set(const char *key, const char *value);
        const char *get(const char *key);
};