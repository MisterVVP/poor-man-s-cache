#include "kvs.h"

KeyValueStore::KeyValueStore(unsigned long long initialSize) : tableSize(initialSize), numEntries(0) {
    isResizing = false;
    table = new Entry[tableSize];
    for (unsigned long long i = 0; i < tableSize; ++i) {
        table[i].occupied = false;
    }
    std::random_device rd;
    std::mt19937_64 e2(rd());
    std::uniform_int_distribution<unsigned long long> dist(
        std::numeric_limits<std::uint64_t>::min(),
        std::numeric_limits<std::uint64_t>::max()
    );
    for (int i = 0; i < HH_KEY_LEN; ++i) {
        hhkey[i] = dist(e2);
    }
}

KeyValueStore::~KeyValueStore() {
    for (unsigned long long i = 0; i < tableSize; ++i) {
        delete[] table[i].key;
        delete[] table[i].value;
    }
    delete[] table;
}

unsigned long long KeyValueStore::hash(const char *key, int attempt) const {
    highwayhash::HHResult64 result;
    highwayhash::InstructionSets::Run<highwayhash::HighwayHash>(hhkey, key, sizeof(key), &result);
    return (result + attempt * attempt) % tableSize;
}

unsigned long long KeyValueStore::rehash(const char *key, int attempt, unsigned long long newTableSize) const {
    highwayhash::HHResult64 result;
    highwayhash::InstructionSets::Run<highwayhash::HighwayHash>(hhkey, key, sizeof(key), &result);
    return (result + attempt * attempt) % newTableSize;
}


void KeyValueStore::resize() {
    isResizing = true;
    std::cout << "Resizing started! numEntries = " << numEntries << " tableSize = " << tableSize << " isResizing = " << isResizing << std::endl;
    auto newTableSize = tableSize * RESIZE_MULTIPLIER;
    auto *newTable = new Entry[newTableSize];
    for (unsigned long long i = 0; i < newTableSize; ++i) {
        newTable[i].occupied = false;
    }

    for (unsigned long long i = 0; i < tableSize; ++i) {
        if (table[i].occupied) {
            int attempt = 0;
            unsigned long long idx;
            do {
                idx = rehash(table[i].key, attempt++, newTableSize);
            } while (newTable[idx].occupied && attempt < newTableSize);

            if (!newTable[idx].occupied) {
                newTable[idx].key = new char[strlen(table[i].key) + 1];
                newTable[idx].value = new char[strlen(table[i].value) + 1];

                strcpy(newTable[idx].key, table[i].key);
                strcpy(newTable[idx].value, table[i].value);
                newTable[idx].occupied = true;

                delete[] table[i].key;
                delete[] table[i].value;
            } 
#ifndef NDEBUG
            else {
                std::cerr << "Resize error: Could not insert key " << table[i].key << std::endl;
            }
#endif
        }
    }

    delete[] table;
    table = newTable;
    tableSize = newTableSize;
    isResizing = false;
    std::cout << "Resizing finished! numEntries = " << numEntries << " tableSize = " << tableSize << " isResizing = " << isResizing << std::endl;
}

bool KeyValueStore::set(const char *key, const char *value) {
    if (numEntries >= (tableSize / RESIZE_MULTIPLIER) && !isResizing) { // Resize when load factor exceeds 50%
        resize();
    }

    int attempt = 0;
    unsigned long long idx;
    do {
        idx = hash(key, attempt++);
#ifndef NDEBUG
        std::cout << "Setting key = " << key << " calculated hash idx = " << idx << " numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;    
#endif
        if (!table[idx].occupied) {
            table[idx].key = new char[strlen(key) + 1];
            table[idx].value = new char[strlen(value) + 1];

            strcpy(table[idx].key, key);
            strcpy(table[idx].value, value);

            table[idx].occupied = true;
            ++numEntries;
            return true;
        } else if (strcmp(table[idx].key, key) == 0) {
            delete[] table[idx].value;

            table[idx].value = new char[strlen(value) + 1];            
            strcpy(table[idx].value, value);
            return true; // Update existing value
        }
    } while (attempt < tableSize);

//#ifndef NDEBUG
    if (attempt > 1) {
        std::cout << "Inserted key = " << key << " attempt = " << attempt << std::endl;
    }
    std::cerr << "Set error: Could not insert key " << key << " numEntries = " << numEntries << " tableSize = " << tableSize<< std::endl;
//#endif
    return false; // Table full
}

const char *KeyValueStore::get(const char *key) {
    int attempt = 0;
    unsigned long long idx;
    do {
        idx = hash(key, attempt++);
#ifndef NDEBUG
        std::cout << "Getting key = " << key << " calculated hash idx = " << idx << std::endl;    
#endif
        if (table[idx].occupied && strcmp(table[idx].key, key) == 0) {
#ifndef NDEBUG
        std::cout << "Returning value = " << table[idx].value << " at key = " << key << " at idx = " << idx << std::endl;    
#endif
            return table[idx].value;
        }
        if (!table[idx].occupied) {
            if (strcmp("non_existent_key", key) != 0) {
                std::cout << "Empty slot found key = " << key << " idx = " << idx << " attempt = " << attempt  << " table[idx].key = " << table[idx].key << " table[idx].value = " << table[idx].value << std::endl;
            }
            break; // Stop searching if an empty slot is found
        }
    } while (attempt < tableSize);
    
    return nullptr; // Key not found
}
