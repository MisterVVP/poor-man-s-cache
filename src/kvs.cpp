#include "kvs.h"

KeyValueStore::KeyValueStore(uint_fast64_t initialSize) : tableSize(initialSize), numEntries(0), numResizes(0), isResizing(false), numFullScans(0) {
    table = new Entry[tableSize];
    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        table[i].occupied = false;
    }
    std::random_device rd;
    std::mt19937_64 e2(rd());
    std::uniform_int_distribution<uint_fast64_t> dist(
        std::numeric_limits<std::uint64_t>::min(),
        std::numeric_limits<std::uint64_t>::max()
    );
    for (int_fast8_t i = 0; i < HH_KEY_LEN; ++i) {
        hhkey[i] = dist(e2);
    }
}

KeyValueStore::~KeyValueStore() {
    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        delete[] table[i].key;
        delete[] table[i].value;
    }
    delete[] table;
}

uint_fast64_t KeyValueStore::calcIndex(uint_fast64_t hash, int attempt, uint_fast64_t tableSize, uint_fast64_t hash2) const
{
    if (hash2) {
        return (hash + attempt * (1 + (hash2 % (tableSize - 1)))) % tableSize;
    } 
    return (hash + attempt * attempt) % tableSize;
}

uint_fast64_t KeyValueStore::hash(const char *key) const
{
    highwayhash::HHResult64 primaryHash;
    highwayhash::InstructionSets::Run<highwayhash::HighwayHash>(hhkey, key, sizeof(key), &primaryHash);
    return primaryHash;
}

// MurmurOAAT64
uint_fast64_t KeyValueStore::hash2(const char *key) const {
    uint_fast64_t hash(525201411107845655ull);
    for (;*key;++key) {
        hash ^= *key;
        hash *= 0x5bd1e9955bd1e995;
        hash ^= hash >> 47;
    }
    return hash;
}


void KeyValueStore::resize() {
    isResizing = true;
#ifndef NDEBUG
    std::cout << "Resizing started! numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;
#endif
    uint_fast64_t newTableSize = tableSize * RESIZE_MULTIPLIER;
    auto *newTable = new Entry[newTableSize];
    for (uint_fast64_t i = 0; i < newTableSize; ++i) {
        newTable[i].occupied = false;
    }

    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        if (table[i].occupied) {
            uint_fast64_t attempt = 0;
            uint_fast64_t idx;
            uint_fast64_t primaryHash = hash(table[i].key);
            uint_fast64_t secondaryHash = 0;
            if (newTableSize >= DOUBLE_HASHING_THRESHOLD) {
                secondaryHash = hash2(table[i].key);
            }

            do {
                idx = calcIndex(primaryHash, attempt++, newTableSize, secondaryHash);
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
    numResizes++;
    std::cout << "Resizing finished! numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;
}

bool KeyValueStore::set(const char *key, const char *value) {
    if (numEntries >= (tableSize / RESIZE_MULTIPLIER) && !isResizing) { // Resize when load factor exceeds 50%
        resize();
    }

    uint_fast64_t attempt = 0;
    uint_fast64_t idx;
    uint_fast64_t primaryHash = hash(key);
    uint_fast64_t secondaryHash = 0;
    if (tableSize >= DOUBLE_HASHING_THRESHOLD) {
        secondaryHash = hash2(key);
    }

    do {
        idx = calcIndex(primaryHash, attempt++, tableSize, secondaryHash);
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

#ifndef NDEBUG
    if (attempt > 1) {
        std::cout << "Inserted key = " << key << " attempt = " << attempt << std::endl;
    }
#endif
    std::cerr << "Set error: Could not insert key " << key << " numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;
    return false; // Table full
}

const char *KeyValueStore::get(const char *key) {
    uint_fast64_t attempt = 0;
    uint_fast64_t idx;
    uint_fast64_t primaryHash = hash(key);
    uint_fast64_t secondaryHash = 0;
    if (tableSize >= DOUBLE_HASHING_THRESHOLD) {
        secondaryHash = hash2(key);
    }

    do {
        idx = calcIndex(primaryHash, attempt++, tableSize, secondaryHash);
#ifndef NDEBUG
        std::cout << "Getting key = " << key << " calculated hash idx = " << idx << std::endl;    
#endif
        if (table[idx].occupied && strcmp(table[idx].key, key) == 0) {
#ifndef NDEBUG
            std::cout << "Returning value = " << table[idx].value << " at key = " << key << " at idx = " << idx << std::endl;    
#endif
            return table[idx].value;
        }
    } while (attempt < MAX_SCAN_ATTEMPTS);

#ifndef NDEBUG
    std::cout << "Performing full scan for key = " << key << " numEntries = " << numEntries << " tableSize = " << tableSize  << " numResizes = " << numResizes << std::endl;
#endif
    for (uint_fast64_t i = 0; i < tableSize - 1; ++i) {
        if (table[i].occupied && strcmp(table[i].key, key) == 0) {
            return table[i].value;
        }
    }
    numFullScans++;
    return nullptr; // Key not found
}
