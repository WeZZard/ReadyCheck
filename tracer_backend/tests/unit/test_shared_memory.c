#include "shared_memory.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

void test_create_and_destroy() {
    printf("Testing shared memory create and destroy...\n");
    
    SharedMemoryRef shm = shared_memory_create_unique("test_shm", 0, shared_memory_get_session_id(), 4096, NULL, 0);
    assert(shm != NULL);
    assert(shared_memory_get_size(shm) == 4096);
    assert(shared_memory_get_address(shm) != NULL);
    assert(shared_memory_is_creator(shm) == true);
    
    // Write some data
    strcpy(shared_memory_get_address(shm), "Hello, Shared Memory!");
    
    // Cleanup
    shared_memory_destroy(shm);
    
    printf("  ✓ Create and destroy test passed\n");
}

void test_open_existing() {
    printf("Testing opening existing shared memory...\n");
    
    // Create shared memory
    SharedMemoryRef shm1 = shared_memory_create_unique("test_open", shared_memory_get_pid(), shared_memory_get_session_id(), 4096, NULL, 0);
    assert(shm1 != NULL);
    
    // Write data
    strcpy(shared_memory_get_address(shm1), "Test Data");
    
    // Open the same shared memory from another handle
    SharedMemoryRef shm2 = shared_memory_open_unique("test_open", shared_memory_get_pid(), shared_memory_get_session_id(), 4096);
    assert(shm2 != NULL);
    assert(shared_memory_is_creator(shm2) == false);
    
    // Verify data is visible
    assert(strcmp(shared_memory_get_address(shm2), "Test Data") == 0);
    
    // Modify data from second handle
    strcpy(shared_memory_get_address(shm2), "Modified Data");
    
    // Verify modification is visible from first handle
    assert(strcmp(shared_memory_get_address(shm1), "Modified Data") == 0);
    
    // Cleanup
    shared_memory_destroy(shm2);
    shared_memory_destroy(shm1);
    
    printf("  ✓ Open existing test passed\n");
}

void test_multiple_regions() {
    printf("Testing multiple shared memory regions...\n");
    
    SharedMemoryRef shm1 = shared_memory_create_unique("region1", 0, shared_memory_get_session_id(), 1024, NULL, 0);
    SharedMemoryRef shm2 = shared_memory_create_unique("region2", 0, shared_memory_get_session_id(), 2048, NULL, 0);
    SharedMemoryRef shm3 = shared_memory_create_unique("region3", 0, shared_memory_get_session_id(), 4096, NULL, 0);
    
    assert(shm1 != NULL);
    assert(shm2 != NULL);
    assert(shm3 != NULL);
    
    // Write different data to each
    strcpy(shared_memory_get_address(shm1), "Region 1");
    strcpy(shared_memory_get_address(shm2), "Region 2");
    strcpy(shared_memory_get_address(shm3), "Region 3");
    
    // Verify independence
    assert(strcmp(shared_memory_get_address(shm1), "Region 1") == 0);
    assert(strcmp(shared_memory_get_address(shm2), "Region 2") == 0);
    assert(strcmp(shared_memory_get_address(shm3), "Region 3") == 0);
    
    // Cleanup
    shared_memory_destroy(shm1);
    shared_memory_destroy(shm2);
    shared_memory_destroy(shm3);
    
    printf("  ✓ Multiple regions test passed\n");
}

void test_large_allocation() {
    printf("Testing large allocation...\n");
    
    size_t size = 32 * 1024 * 1024; // 32MB
    SharedMemoryRef shm = shared_memory_create_unique("large_shm", 0, shared_memory_get_session_id(), size, NULL, 0);
    assert(shm != NULL);
    assert(shared_memory_get_size(shm) == size);
    
    // Write pattern at beginning and end
    memset(shared_memory_get_address(shm), 0xAA, 1024);
    memset((char*)shared_memory_get_address(shm) + size - 1024, 0xBB, 1024);
    
    // Verify pattern
    unsigned char* data = (unsigned char*)shared_memory_get_address(shm);
    assert(data[0] == 0xAA);
    assert(data[1023] == 0xAA);
    assert(data[size - 1024] == 0xBB);
    assert(data[size - 1] == 0xBB);
    
    shared_memory_destroy(shm);
    
    printf("  ✓ Large allocation test passed\n");
}

int main() {
    printf("\n=== Shared Memory Unit Tests ===\n\n");
    
    test_create_and_destroy();
    test_open_existing();
    test_multiple_regions();
    test_large_allocation();
    
    printf("\n✅ All shared memory tests passed!\n\n");
    return 0;
}