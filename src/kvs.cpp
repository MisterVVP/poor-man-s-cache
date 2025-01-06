#include "kvs.h"

int KeyValueStore::hash(const char *key)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash % TABLE_SIZE;
}

bool KeyValueStore::set(const char *key, const char *value)
{
    int idx = hash(key);
    for (int i = 0; i < TABLE_SIZE; ++i) {
        int probe = (idx + i) % TABLE_SIZE;
        if (!table[probe].occupied || strcmp(table[probe].key, key) == 0) {
            strncpy(table[probe].key, key, sizeof(table[probe].key));
            strncpy(table[probe].value, value, sizeof(table[probe].value));
            table[probe].occupied = true;
            return true;
        }
    }
    return false; // Table full
}

const char *KeyValueStore::get(const char *key)
{
    int idx = hash(key);
    for (int i = 0; i < TABLE_SIZE; ++i) {
        int probe = (idx + i) % TABLE_SIZE;
        if (table[probe].occupied && strcmp(table[probe].key, key) == 0) {
            return table[probe].value;
        }
        if (!table[probe].occupied) {
            break;
        }
    }
    return nullptr; // Key not found
}
