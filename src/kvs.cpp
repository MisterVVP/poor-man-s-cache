#include "kvs.h"

KeyValueStore::KeyValueStore(uint_fast64_t initialSize) : tableSize(initialSize), numEntries(0), numResizes(0), numCollisions(0), isResizing(false) {
    std::cout << "Table initialization started! initialSize = " << initialSize << std::endl;
    table = new Bucket[tableSize];
    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            table[i].entries[j].occupied = false;
        }
    }
    generatePrimeQueue();
    std::cout << "Table initialization finished!" << std::endl;
}

KeyValueStore::~KeyValueStore() {
    uint_fast64_t emptyEntries = 0;
    uint_fast64_t emptyBuckets = 0;
    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            if (!table[i].entries[j].occupied) {
                ++emptyEntries;
                if (j == 0) {
                    ++emptyBuckets;
                }
            }
        }
    }
    std::cout << "Key value storage is being destroyed! numCollisions = " << numCollisions << " emptyBuckets = "
              << emptyBuckets << " emptyEntries = " << emptyEntries << " tableSize = " << tableSize 
              << " numEntries = " << numEntries << " numResizes = " << numResizes << std::endl;
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
    primeQueue.push(prime);

    double growthFactor = 2.0; // Start with a higher growth factor for small primes
    while (prime < std::numeric_limits<uint_fast64_t>::max() / 10) {
        uint_fast64_t nextCandidate = static_cast<uint_fast64_t>(prime * growthFactor);
        prime = nextPrime(nextCandidate); // Get the next prime >= nextCandidate
        primeQueue.push(prime);

        // Dynamically adjust the growth factor
        if (prime < 100000) {
            growthFactor = 4;
        } else if (prime < 100000) {
            growthFactor = 2; // Faster growth for small primes
        } else if (prime < 1000000) {
            growthFactor = 1.5; // Moderate growth for medium primes
        } else if (prime < 10000000) {
            growthFactor = 1.2; // Slower growth for larger primes
        } else if (prime < 100000000) {
            growthFactor = 1.1; // Moderate growth for medium primes
        } else {
            growthFactor = 1.05; // Very slow growth for very large primes
        }
    }
}
void KeyValueStore::cleanTable(Bucket *tableToDelete, uint_fast64_t size) {
#ifndef NDEBUG
    std::cout << "Table cleanup started! size = " << size << std::endl;
#endif
    for (uint_fast64_t i = 0; i < size; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            if (tableToDelete[i].entries[j].occupied) {
                delete[] tableToDelete[i].entries[j].key;
                delete[] tableToDelete[i].entries[j].value;
            }
        }
    }
    delete[] tableToDelete;
#ifndef NDEBUG
    std::cout << "Table cleanup finished!" << std::endl;
#endif
}

inline uint_fast64_t KeyValueStore::calcIndex(uint_fast64_t hash, int attempt, uint_fast64_t tableSize) const {
    return (hash + attempt * attempt) % tableSize;
}

//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this function source code.
//-----------------------------------------------------------------------------
uint_fast64_t KeyValueStore::hash(const char *key) const {
    uint_fast64_t hash(525201411107845655ull);
    for (; *key; ++key) {
        hash ^= *key;
        hash *= 0x5bd1e9955bd1e995;
        hash ^= hash >> 47;
    }
    return hash;
}
//-----------------------------------------------------------------------------

void KeyValueStore::resize() {
    isResizing = true;
#ifndef NDEBUG
    std::cout << "Resizing started! numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;
#endif
    if (primeQueue.empty()) {
        throw std::runtime_error("Max hashtable size reached. Cannot resize further.");
    }

    // Get the next table size from the prime queue
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

                bool inserted = false;
                do {
                    idx = calcIndex(primaryHash, attempt++, newTableSize);
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
                } while (!inserted && attempt < MAX_READ_WRITE_ATTEMPTS);
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
#ifndef NDEBUG
    std::cout << "Resizing finished! numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;
#endif
}

bool KeyValueStore::set(const char *key, const char *value) {
    if (numEntries >= ((tableSize * RESIZE_THRESHOLD_PERCENTAGE) / 100)  && !isResizing) {
        resize();
    }

    uint_fast64_t attempt = 0;
    uint_fast64_t idx;
    uint_fast64_t primaryHash = hash(key);
    do {
        idx = calcIndex(primaryHash, attempt++, tableSize);
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

                table[idx].entries[i].value = new char[strlen(value) + 1];
                strcpy(table[idx].entries[i].value, value);

                // Ensure the entry is marked as occupied, even if overwritten
                table[idx].entries[i].occupied = true;
                return true;
            }
        }
        numCollisions++;
    } while (attempt < MAX_READ_WRITE_ATTEMPTS);


#ifndef NDEBUG
    std::cerr << "Failed to insert key = " << key << " after " << attempt << " attempts." << std::endl;
#endif
    return false;
}

const char *KeyValueStore::get(const char *key) {
    uint_fast64_t attempt = 0;
    uint_fast64_t idx;
    uint_fast64_t primaryHash = hash(key);
    do {
        idx = calcIndex(primaryHash, attempt++, tableSize);
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            if (table[idx].entries[i].occupied && strcmp(table[idx].entries[i].key, key) == 0){
                return table[idx].entries[i].value;
            }     
        }     
    } while (attempt < MAX_READ_WRITE_ATTEMPTS);

#ifndef NDEBUG
    std::cout << "Unable to find key = " << key << " at idx = " << idx << " numEntries = " << numEntries << " tableSize = " << tableSize  << " attempt = " << attempt << std::endl;
#endif

    return nullptr; // Key not found
}
