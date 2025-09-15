#ifndef SHM_DIRECTORY_H
#define SHM_DIRECTORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <tracer_backend/utils/tracer_types.h>

// Map local base pointers for all entries in the directory.
// Returns true if at least one entry was successfully mapped.
bool shm_dir_map_local_bases(const ShmDirectory* dir);

// Clear and unmap all local bases created by map_local_bases
void shm_dir_clear_local_bases(void);

// Get mapped base pointer and size for an entry index (or NULL/0 if unmapped)
void* shm_dir_get_base(uint32_t idx);
size_t shm_dir_get_size(uint32_t idx);

#ifdef __cplusplus
}
#endif

#endif // SHM_DIRECTORY_H

