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
    generatePrimeQueue();
}

KeyValueStore::~KeyValueStore() {
    cleanTable(table, tableSize);
}

bool KeyValueStore::isPrime(uint_fast64_t n) const {
    if (n < 2) return false;
    if (n % 2 == 0 && n > 2) return false;
    for (uint_fast64_t i = 3; i <= std::sqrt(n); i += 2) {
        if (n % i == 0) return false;
    }
    return true;
}

uint_fast64_t KeyValueStore::nextPrime(uint_fast64_t start) const {
    while (!isPrime(start)) {
        ++start;
    }
    return start;
}

void KeyValueStore::generatePrimeQueue() {
    uint_fast64_t prime = 2053; // Start with the first prime
    while (prime < std::numeric_limits<uint_fast64_t>::max() / RESIZE_MULTIPLIER) {
        primeQueue.push(prime);
        prime = nextPrime(prime * RESIZE_MULTIPLIER);
    }
}

void KeyValueStore::cleanTable(Bucket *tableToDelete, uint_fast64_t size) {
    std::cout << "Table cleanup started! size = " << size << std::endl;
    for (uint_fast64_t i = 0; i < size; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            if (tableToDelete[i].entries[j].occupied) {
                delete[] tableToDelete[i].entries[j].key;
                delete[] tableToDelete[i].entries[j].value;
            }
        }
    }
    delete[] tableToDelete;
    std::cout << "Table cleanup finished!" << std::endl;     
}

uint_fast64_t KeyValueStore::calcIndex(uint_fast64_t hash, int attempt, uint_fast64_t tableSize, uint_fast64_t hash2) const {
    if (hash2) {
        return (hash + attempt * (1 + (hash2 % (tableSize - 1)))) % tableSize;
    } 
    return (hash + attempt * attempt) % tableSize;
}

uint_fast64_t KeyValueStore::hash(const char *key) const {
    highwayhash::HHResult64 primaryHash;
    highwayhash::InstructionSets::Run<highwayhash::HighwayHash>(hhkey, key, sizeof(key), &primaryHash);
    return primaryHash;
}

uint_fast64_t KeyValueStore::hash2(const char *key) const {
    uint_fast64_t hash(525201411107845655ull);
    for (; *key; ++key) {
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
    if (primeQueue.empty()) {
        throw std::runtime_error("Max hashtable size was reached. Cannot resize further.");
    }

    uint_fast64_t newTableSize = primeQueue.front();
    primeQueue.pop();
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
                if (!inserted) {
                    std::cerr << "Resize Error: Could not insert key during migration." << std::endl;
                }
            }
        }
    }

    cleanTable(table, tableSize);
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
                //delete[] table[idx].entries[i].value;

                table[idx].entries[i].value = new char[strlen(value) + 1];
                strcpy(table[idx].entries[i].value, value);

                // Ensure the entry is marked as occupied, even if overwritten
                table[idx].entries[i].occupied = true;
                return true;
            }
        }
    } while (attempt < tableSize);

#ifndef NDEBUG
    std::cerr << "Failed to insert key = " << key << " after " << attempt << " attempts." << std::endl;
#endif
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
            if (table[idx].entries[i].occupied && strcmp(table[idx].entries[i].key, key) == 0){
                return table[idx].entries[i].value;
            }
            if (!table[idx].entries[i].occupied) {
        #ifndef NDEBUG
                std::cout << "Found empty space for key = " << key << " at idx = " << idx << " at entry = " << i << " tableSize = " << tableSize  << " attempt = " << attempt << std::endl;
        #endif
                for (uint_fast64_t a = 0; a < tableSize - 1; ++a) {
                    for (int b = 0; b < BUCKET_SIZE; ++b) {
                        if (table[a].entries[b].occupied && strcmp(table[a].entries[b].key, key) == 0) {
                            numFullScans++;  
                            #ifndef NDEBUG
                            std::cout << "Performing full scan for key = " << key << " value = " << table[a].entries[b].value << std::endl;
                            #endif

                            table[idx].entries[i].key = new char[strlen(table[a].entries[b].key) + 1];
                            table[idx].entries[i].value = new char[strlen(table[a].entries[b].value) + 1];
                            table[idx].entries[i].occupied = true;

                            strcpy(table[idx].entries[i].key, table[a].entries[b].key);
                            strcpy(table[idx].entries[i].value, table[a].entries[b].value);

                            #ifndef NDEBUG
                            std::cout << "Moved key = " << key << " from index = " << a << " and entry = " << b << " to index = " << idx << " and entry = " << i  << std::endl;
                            #endif
                            return table[idx].entries[i].value;
                        }
                    }
                }
            }        
        }     
    } while (attempt < MAX_READ_WRITE_ATTEMPTS);

#ifndef NDEBUG
    if (attempt == MAX_READ_WRITE_ATTEMPTS) {
        std::cout << "Unable to find key = " << key << " at idx = " << idx << " numEntries = " << numEntries << " tableSize = " << tableSize  << " attempt = " << attempt << std::endl;
    }
#endif

    return nullptr; // Key not found
}
