#include "kvs.h"

KeyValueStore::KeyValueStore(int initialSize) : tableSize(initialSize), numEntries(0) {
    table = new Entry[tableSize];
    for (int i = 0; i < tableSize; ++i) {
        table[i].occupied = false;
    }
}

KeyValueStore::~KeyValueStore() {
    delete[] table;
}

// MurmurOAAT64
int KeyValueStore::hash(const char *key, int attempt) const {
    unsigned long hash(525201411107845655ull);
    for (;*key;++key) {
        hash ^= *key;
        hash *= 0x5bd1e9955bd1e995;
        hash ^= hash >> 47;
    }
    return (hash + attempt * attempt) % tableSize; // Quadratic probing
}

void KeyValueStore::resize() {
    int newTableSize = tableSize * 2;
    Entry *newTable = new Entry[newTableSize];
    for (int i = 0; i < newTableSize; ++i) {
        newTable[i].occupied = false;
    }

    for (int i = 0; i < tableSize; ++i) {
        if (table[i].occupied) {
            int attempt = 0;
            int idx;
            do {
                idx = hash(table[i].key, attempt++);
            } while (newTable[idx].occupied);

            strncpy(newTable[idx].key, table[i].key, sizeof(newTable[idx].key));
            strncpy(newTable[idx].value, table[i].value, sizeof(newTable[idx].value));
            newTable[idx].occupied = true;
        }
    }

    delete[] table;
    table = newTable;
    tableSize = newTableSize;
}

bool KeyValueStore::set(const char *key, const char *value) {
    if (numEntries >= tableSize / 2) { // Resize when load factor exceeds 50%
        resize();
    }

    int attempt = 0;
    int idx;
    do {
        idx = hash(key, attempt++);
        if (!table[idx].occupied || strcmp(table[idx].key, key) == 0) {
            strncpy(table[idx].key, key, sizeof(table[idx].key));
            strncpy(table[idx].value, value, sizeof(table[idx].value));
            table[idx].occupied = true;
            if (strcmp(table[idx].key, key) != 0) {
                ++numEntries;
            }
            return true;
        }
    } while (attempt < tableSize);

    return false; // Table full
}

const char *KeyValueStore::get(const char *key) {
    int attempt = 0;
    int idx;
    do {
        idx = hash(key, attempt++);
        if (table[idx].occupied && strcmp(table[idx].key, key) == 0) {
            return table[idx].value;
        }
        if (!table[idx].occupied) {
            break;
        }
    } while (attempt < tableSize);

    return nullptr; // Key not found
}