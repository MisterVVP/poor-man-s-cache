#include "kvs.h"

KeyValueStore::KeyValueStore(uint_fast64_t initialSize) : tableSize(initialSize), numEntries(0), numResizes(0), isResizing(false), numFullScans(0) {
    table = new Bucket[tableSize];
    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            table[i].entries[j].occupied = false;
        }
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
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            delete[] table[i].entries[j].key;
            delete[] table[i].entries[j].value;
        }
    }
    delete[] table;
}

uint_fast64_t KeyValueStore::calcIndex(uint_fast64_t hash, int attempt, uint_fast64_t tableSize, uint_fast64_t hash2) const {
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
    auto *newTable = new Bucket[newTableSize];
    for (uint_fast64_t i = 0; i < newTableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            newTable[i].entries[j].occupied = false;
        }
    }


    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            if (table[i].entries[j].occupied) {
                const char* key = table[i].entries[j].key;
                const char* value = table[i].entries[j].value;

                uint_fast64_t attempt = 0;
                uint_fast64_t idx;
                uint_fast64_t primaryHash = hash(key);
                uint_fast64_t secondaryHash = 0;
                if (newTableSize >= DOUBLE_HASHING_THRESHOLD) {
                    secondaryHash = hash2(key);
                }

                bool inserted = false;
                do {
                    idx = calcIndex(primaryHash, attempt++, newTableSize, secondaryHash);
                    for (int k = 0; k < BUCKET_SIZE; ++k) {
                        if (!newTable[idx].entries[k].occupied) {
                            newTable[idx].entries[k].key = new char[strlen(key) + 1];
                            newTable[idx].entries[k].value = new char[strlen(value) + 1];

                            strcpy(newTable[idx].entries[k].key, key);
                            strcpy(newTable[idx].entries[k].value, value);
                            newTable[idx].entries[k].occupied = true;
                            inserted = true;
                            break;
                        }
                    }
                } while (!inserted && attempt < newTableSize);
                delete[] key;
                delete[] value;
            }
        }
    }

    delete[] table;
    table = newTable;
    tableSize = newTableSize;
    isResizing = false;
    ++numResizes;
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
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            if (!table[idx].entries[i].occupied) {
                table[idx].entries[i].key = new char[strlen(key) + 1];
                table[idx].entries[i].value = new char[strlen(value) + 1];

                strcpy(table[idx].entries[i].key, key);
                strcpy(table[idx].entries[i].value, value);

                table[idx].entries[i].occupied = true;
                ++numEntries;
                return true;
            } else if (strcmp(table[idx].entries[i].key, key) == 0) {
                delete[] table[idx].entries[i].value;

                table[idx].entries[i].value = new char[strlen(value) + 1];
                strcpy(table[idx].entries[i].value, value);
                return true;
            }
        }
    } while (attempt < tableSize);

#ifndef NDEBUG
    if (attempt > 1) {
        std::cout << "Inserted key = " << key << " attempt = " << attempt << std::endl;
    }
#endif
    std::cerr << "Set error: Could not insert key " << key << " numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;
    return false;
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
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            if (table[idx].entries[i].occupied && strcmp(table[idx].entries[i].key, key) == 0) {
                return table[idx].entries[i].value;
            }
        }
    } while (attempt < MAX_SCAN_ATTEMPTS);


#ifndef NDEBUG
    std::cout << "Performing full scan for key = " << key << " numEntries = " << numEntries << " tableSize = " << tableSize  << " numResizes = " << numResizes << std::endl;
#endif
    for (uint_fast64_t i = 0; i < tableSize - 1; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            if (table[i].entries[j].occupied && strcmp(table[i].entries[j].key, key) == 0) {
                numFullScans++;
                auto val = table[i].entries[j].value;
#ifndef NDEBUG
                std::cout << "Forcefully overwriting key = " << key << " value = " << val << std::endl;
#endif
                this->set(key, val);
                return val;
            }
        }
    }
    numFullScans++;
    return nullptr; // Key not found
}
