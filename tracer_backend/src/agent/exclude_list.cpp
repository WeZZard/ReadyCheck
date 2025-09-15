// Lightweight, high-performance exclude set for symbol names
// Implementation uses an open-addressing hash table of 64-bit hashes
// for O(1) average contains() checks.

#include <tracer_backend/agent/exclude_list.h>

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

struct AdaExcludeList {
    uint64_t* slots;
    size_t capacity; // power-of-two number of slots
    size_t count;
};

static inline size_t next_pow2(size_t x) {
    if (x < 8) return 8;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
#if ULONG_MAX > 0xffffffffUL
    x |= x >> 32;
#endif
    return x + 1;
}

uint64_t ada_exclude_hash(const char* name) {
    if (!name) return 0ULL;
    // FNV-1a 64-bit, case-insensitive (ASCII)
    const uint64_t FNV_OFFSET = 1469598103934665603ULL;
    const uint64_t FNV_PRIME  = 1099511628211ULL;
    uint64_t h = FNV_OFFSET;
    for (const unsigned char* p = (const unsigned char*)name; *p; ++p) {
        unsigned char c = *p;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32); // tolower fast path
        h ^= (uint64_t)c;
        h *= FNV_PRIME;
    }
    return (h == 0ULL) ? 0x9e3779b97f4a7c15ULL : h; // avoid zero sentinel
}

AdaExcludeList* ada_exclude_create(size_t capacity_hint) {
    AdaExcludeList* xs = (AdaExcludeList*)calloc(1, sizeof(AdaExcludeList));
    if (!xs) return nullptr;
    size_t cap = next_pow2(capacity_hint ? capacity_hint : 128);
    xs->slots = (uint64_t*)calloc(cap, sizeof(uint64_t));
    if (!xs->slots) { free(xs); return nullptr; }
    xs->capacity = cap;
    xs->count = 0;
    return xs;
}

void ada_exclude_destroy(AdaExcludeList* xs) {
    if (!xs) return;
    free(xs->slots);
    free(xs);
}

static inline bool insert_hash(AdaExcludeList* xs, uint64_t h) {
    size_t mask = xs->capacity - 1;
    size_t i = (size_t)h & mask;
    for (size_t probe = 0; probe < xs->capacity; ++probe) {
        if (xs->slots[i] == 0ULL) {
            xs->slots[i] = h;
            xs->count++;
            return true;
        } else if (xs->slots[i] == h) {
            return true; // already present
        }
        i = (i + 1) & mask;
    }
    return false; // table full (should not happen as we overprovision)
}

bool ada_exclude_add(AdaExcludeList* xs, const char* name) {
    if (!xs || !name || !*name) return false;
    uint64_t h = ada_exclude_hash(name);
    // Maintain load factor < 0.7
    if ((xs->count + 1) * 10 > xs->capacity * 7) {
        size_t new_cap = xs->capacity << 1;
        uint64_t* old = xs->slots;
        xs->slots = (uint64_t*)calloc(new_cap, sizeof(uint64_t));
        if (!xs->slots) { xs->slots = old; return false; }
        size_t old_cap = xs->capacity;
        xs->capacity = new_cap;
        xs->count = 0;
        for (size_t i = 0; i < old_cap; ++i) {
            if (old[i] != 0ULL) insert_hash(xs, old[i]);
        }
        free(old);
    }
    return insert_hash(xs, h);
}

bool ada_exclude_contains_hash(AdaExcludeList* xs, uint64_t hash) {
    if (!xs || hash == 0ULL) return false;
    size_t mask = xs->capacity - 1;
    size_t i = (size_t)hash & mask;
    for (size_t probe = 0; probe < xs->capacity; ++probe) {
        uint64_t slot = xs->slots[i];
        if (slot == 0ULL) return false; // not found
        if (slot == hash) return true;
        i = (i + 1) & mask;
    }
    return false;
}

bool ada_exclude_contains(AdaExcludeList* xs, const char* name) {
    return ada_exclude_contains_hash(xs, ada_exclude_hash(name));
}

void ada_exclude_add_defaults(AdaExcludeList* xs) {
    // Hotspots and reentrancy-prone APIs (platform-agnostic subset)
    const char* defaults[] = {
        "malloc", "free", "calloc", "realloc",
        "memcpy", "memmove", "memset", "bzero",
        "strcpy", "strncpy", "strlen", "strcmp",
        "objc_msgSend", "objc_release", "objc_retain",
        "pthread_mutex_lock", "pthread_mutex_unlock",
        "pthread_once", "pthread_create",
        "gum_interceptor_attach", "gum_interceptor_detach",
        "gum_interceptor_begin_transaction", "gum_interceptor_end_transaction",
        "_malloc", "_free", // symbol variations
        nullptr
    };
    for (const char** p = defaults; *p; ++p) {
        ada_exclude_add(xs, *p);
    }
}

static inline void trim_space(const char** begin, const char** end) {
    while (*begin < *end && isspace((unsigned char)**begin)) (*begin)++;
    while (*end > *begin && isspace((unsigned char)*((*end) - 1))) (*end)--;
}

void ada_exclude_add_from_csv(AdaExcludeList* xs, const char* csv) {
    if (!xs || !csv || !*csv) return;
    const char* p = csv;
    while (*p) {
        const char* start = p;
        const char* end = p;
        while (*end && *end != ',' && *end != ';') end++;
        // Trim spaces
        const char* s = start;
        const char* e = end;
        trim_space(&s, &e);
        if (e > s) {
            char buf[256];
            size_t n = (size_t)(e - s);
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            memcpy(buf, s, n);
            buf[n] = '\0';
            ada_exclude_add(xs, buf);
        }
        p = (*end) ? end + 1 : end;
    }
}

