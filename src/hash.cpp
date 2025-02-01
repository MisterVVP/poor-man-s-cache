#include "hash.h"

/*
uint_fast64_t hashFunc(const char *key) const {
    uint_fast64_t len = strlen(key);
    uint_fast64_t hash_otpt[2];
    MurmurHash3_x64_128(key, len, 0, &hash_otpt); //TODO, generate seed once during runtime
    return hash_otpt[0];
}
*/


uint_fast64_t hashFunc(const char *key) {
    uint_fast64_t hash(525201411107845655ull);
    for (; *key; ++key) {
        hash ^= *key;
        hash *= 0x5bd1e9955bd1e995;
        hash ^= hash >> 47;
    }
    return hash;
}