#include <tracer_backend/utils/shm_directory.h>
#include <tracer_backend/utils/shared_memory.h>
#include <string.h>

typedef struct {
    SharedMemoryRef handle;
    void* base;
    size_t size;
    char name[64];
    bool in_use;
} shm_mapping_t;

static shm_mapping_t g_mappings[8];
static uint32_t g_mapping_count = 0;

void shm_dir_clear_local_bases(void) {
    for (uint32_t i = 0; i < 8; i++) {
        if (g_mappings[i].in_use) {
            if (g_mappings[i].handle) {
                shared_memory_destroy(g_mappings[i].handle);
            }
            memset(&g_mappings[i], 0, sizeof(g_mappings[i]));
        }
    }
    g_mapping_count = 0;
}

bool shm_dir_map_local_bases(const ShmDirectory* dir) {
    if (!dir) return false;
    shm_dir_clear_local_bases();
    uint32_t ok = 0;
    uint32_t count = dir->count;
    if (count > 8) count = 8;
    for (uint32_t i = 0; i < count; i++) {
        const ShmEntry* e = &dir->entries[i];
        if (e->name[0] == '\0' || e->size == 0) continue;
        SharedMemoryRef h = shared_memory_open_named(e->name, (size_t)e->size);
        if (!h) continue;
        g_mappings[i].handle = h;
        g_mappings[i].base = shared_memory_get_address(h);
        g_mappings[i].size = shared_memory_get_size(h);
        strncpy(g_mappings[i].name, e->name, sizeof(g_mappings[i].name) - 1);
        g_mappings[i].in_use = true;
        ok++;
    }
    g_mapping_count = count;
    return ok > 0;
}

void* shm_dir_get_base(uint32_t idx) {
    if (idx >= 8) return NULL;
    if (!g_mappings[idx].in_use) return NULL;
    return g_mappings[idx].base;
}

size_t shm_dir_get_size(uint32_t idx) {
    if (idx >= 8) return 0;
    if (!g_mappings[idx].in_use) return 0;
    return g_mappings[idx].size;
}

