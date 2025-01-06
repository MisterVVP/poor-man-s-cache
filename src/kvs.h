#pragma once
#include <cstring>

// Custom key-value store implementation
class KeyValueStore {
    private:
        static const int TABLE_SIZE = 1024;
        struct Entry {
            char key[64];
            char value[256];
            bool occupied;
        };

        Entry table[TABLE_SIZE];

        int hash(const char *key);

    public:
        KeyValueStore() {
            for (int i = 0; i < TABLE_SIZE; ++i) {
                table[i].occupied = false;
            }
        }

        bool set(const char *key, const char *value);
        const char *get(const char *key);
};