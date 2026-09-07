
#pragma once

#include <sys/types.h>

typedef u_int32_t hash_t;

hash_t hash(const char *key);

#ifdef CEMBED_IMPLEMENTATION

hash_t hash(const char *key) { // Hash Function: MurmurOAAT64
  hash_t h = 3323198485ul;
  for (; *key; ++key) {
    h ^= *key;
    h *= 0x5bd1e995;
    h ^= h >> 15;
  }
  return h;
}

#endif
