// thread_registry_cpp.cpp - C++ implementation with C compatibility layer

#include "thread_registry_cpp.hpp"
#include <cstdlib>
#include <cstring>
#include <cassert>

namespace ada {

// Static TLS for fast path
thread_local ThreadLaneSetCpp* tls_my_lanes_cpp = nullptr;

}  // namespace ada

// =============================================================================
// C Compatibility Layer - Wraps C++ implementation for C interface
// =============================================================================

extern "C" {

#include "thread_registry.h"

// TLS variable for C compatibility
__thread ThreadLaneSet* tls_my_lanes = nullptr;

// Forward declaration
void thread_registry_set_my_lanes(ThreadLaneSet* lanes);

// Use C++ implementation but expose C interface
ThreadRegistry* thread_registry_init(void* memory, size_t size) {
    auto* cpp_registry = ada::ThreadRegistryCpp::create(memory, size);
    return reinterpret_cast<ThreadRegistry*>(cpp_registry);
}

void thread_registry_deinit(ThreadRegistry* registry) {
    if (!registry) return;
    auto* cpp_registry = reinterpret_cast<ada::ThreadRegistryCpp*>(registry);
    cpp_registry->~ThreadRegistryCpp();
}

ThreadLaneSet* thread_registry_register(ThreadRegistry* registry, uintptr_t thread_id) {
    if (!registry) return nullptr;
    auto* cpp_registry = reinterpret_cast<ada::ThreadRegistryCpp*>(registry);
    auto* cpp_lanes = cpp_registry->register_thread(thread_id);
    ThreadLaneSet* lanes = reinterpret_cast<ThreadLaneSet*>(cpp_lanes);
    
    // Set TLS for fast path access
    if (lanes) {
        thread_registry_set_my_lanes(lanes);
    }
    
    return lanes;
}

ThreadLaneSet* thread_registry_get_thread_lanes(ThreadRegistry* registry, uintptr_t thread_id) {
    if (!registry) return nullptr;
    auto* cpp_registry = reinterpret_cast<ada::ThreadRegistryCpp*>(registry);
    
    // Search for existing registration
    for (uint32_t i = 0; i < cpp_registry->thread_count.load(); ++i) {
        if (cpp_registry->thread_lanes[i].thread_id == thread_id && 
            cpp_registry->thread_lanes[i].active.load()) {
            return reinterpret_cast<ThreadLaneSet*>(&cpp_registry->thread_lanes[i]);
        }
    }
    return nullptr;
}

ThreadLaneSet* thread_registry_get_my_lanes(void) {
    return tls_my_lanes;
}

void thread_registry_set_my_lanes(ThreadLaneSet* lanes) {
    tls_my_lanes = lanes;
    ada::tls_my_lanes_cpp = reinterpret_cast<ada::ThreadLaneSetCpp*>(lanes);
}

void thread_registry_unregister(ThreadLaneSet* lanes) {
    if (!lanes) return;
    auto* cpp_lanes = reinterpret_cast<ada::ThreadLaneSetCpp*>(lanes);
    cpp_lanes->active.store(false);
}

bool thread_registry_unregister_by_id(ThreadRegistry* registry, uintptr_t thread_id) {
    if (!registry) return false;
    auto* cpp_registry = reinterpret_cast<ada::ThreadRegistryCpp*>(registry);
    
    // Find and deactivate thread
    for (uint32_t i = 0; i < cpp_registry->thread_count.load(); ++i) {
        if (cpp_registry->thread_lanes[i].thread_id == thread_id) {
            cpp_registry->thread_lanes[i].active.store(false);
            return true;
        }
    }
    return false;
}

uint32_t thread_registry_get_active_count(ThreadRegistry* registry) {
    if (!registry) return 0;
    auto* cpp_registry = reinterpret_cast<ada::ThreadRegistryCpp*>(registry);
    
    uint32_t count = 0;
    for (uint32_t i = 0; i < cpp_registry->thread_count.load(); ++i) {
        if (cpp_registry->thread_lanes[i].active.load()) {
            count++;
        }
    }
    return count;
}

ThreadLaneSet* thread_registry_get_thread_at(ThreadRegistry* registry, uint32_t index) {
    if (!registry || index >= MAX_THREADS) return nullptr;
    auto* cpp_registry = reinterpret_cast<ada::ThreadRegistryCpp*>(registry);
    
    if (index >= cpp_registry->thread_count.load()) return nullptr;
    if (!cpp_registry->thread_lanes[index].active.load()) return nullptr;
    
    return reinterpret_cast<ThreadLaneSet*>(&cpp_registry->thread_lanes[index]);
}

void thread_registry_stop_accepting(ThreadRegistry* registry) {
    if (!registry) return;
    auto* cpp_registry = reinterpret_cast<ada::ThreadRegistryCpp*>(registry);
    cpp_registry->accepting_registrations.store(false);
}

void thread_registry_request_shutdown(ThreadRegistry* registry) {
    if (!registry) return;
    auto* cpp_registry = reinterpret_cast<ada::ThreadRegistryCpp*>(registry);
    cpp_registry->shutdown_requested.store(true);
}

bool thread_registry_is_shutdown_requested(ThreadRegistry* registry) {
    if (!registry) return true;
    auto* cpp_registry = reinterpret_cast<ada::ThreadRegistryCpp*>(registry);
    return cpp_registry->shutdown_requested.load();
}

// Lane operations - delegate to C++ implementation
bool lane_submit_ring(Lane* lane, uint32_t ring_idx) {
    if (!lane) return false;
    auto* cpp_lane = reinterpret_cast<ada::LaneCpp*>(lane);
    return cpp_lane->submit_ring(ring_idx);
}

uint32_t lane_take_ring(Lane* lane) {
    if (!lane) return UINT32_MAX;
    auto* cpp_lane = reinterpret_cast<ada::LaneCpp*>(lane);
    
    auto head = cpp_lane->submit_head.load(std::memory_order_relaxed);
    auto tail = cpp_lane->submit_tail.load(std::memory_order_acquire);
    
    if (head == tail) return UINT32_MAX;  // Queue empty
    
    uint32_t ring_idx = cpp_lane->memory_layout->submit_queue[head];
    cpp_lane->submit_head.store((head + 1) % QUEUE_COUNT_INDEX_LANE, 
                                std::memory_order_release);
    return ring_idx;
}

bool lane_return_ring(Lane* lane, uint32_t ring_idx) {
    if (!lane || ring_idx >= lane->ring_count) return false;
    auto* cpp_lane = reinterpret_cast<ada::LaneCpp*>(lane);
    
    auto head = cpp_lane->free_head.load(std::memory_order_relaxed);
    auto tail = cpp_lane->free_tail.load(std::memory_order_acquire);
    auto next = (tail + 1) % QUEUE_COUNT_INDEX_LANE;
    
    if (next == head) return false;  // Queue full
    
    cpp_lane->memory_layout->free_queue[tail] = ring_idx;
    cpp_lane->free_tail.store(next, std::memory_order_release);
    return true;
}

uint32_t lane_get_free_ring(Lane* lane) {
    if (!lane) return UINT32_MAX;
    auto* cpp_lane = reinterpret_cast<ada::LaneCpp*>(lane);
    
    auto head = cpp_lane->free_head.load(std::memory_order_relaxed);
    auto tail = cpp_lane->free_tail.load(std::memory_order_acquire);
    
    if (head == tail) return UINT32_MAX;  // Queue empty
    
    uint32_t ring_idx = cpp_lane->memory_layout->free_queue[head];
    cpp_lane->free_head.store((head + 1) % QUEUE_COUNT_INDEX_LANE, 
                              std::memory_order_release);
    return ring_idx;
}

// lane_get_active_ring is already defined as inline in header

bool lane_swap_active_ring(Lane* lane) {
    if (!lane) return false;
    auto* cpp_lane = reinterpret_cast<ada::LaneCpp*>(lane);
    
    // Get a free ring
    uint32_t new_idx = lane_get_free_ring(lane);
    if (new_idx == UINT32_MAX) return false;
    
    // Swap active ring
    uint32_t old_idx = cpp_lane->active_idx.exchange(new_idx);
    
    // Submit old ring for draining
    return lane_submit_ring(lane, old_idx);
}

// Debug functions
void thread_registry_dump(ThreadRegistry* registry) {
    if (!registry) return;
    auto* cpp_registry = reinterpret_cast<ada::ThreadRegistryCpp*>(registry);
    cpp_registry->debug_dump();
}

size_t thread_registry_calculate_memory_size(void) {
    // Calculate total memory needed:
    // - ThreadRegistryCpp object + trailing LaneMemoryLayout structures
    size_t struct_size = ada::ThreadRegistryCpp::totalSizeNeeded(MAX_THREADS, MAX_THREADS);
    
    // - Ring buffer memory for all lanes
    size_t index_ring_total = MAX_THREADS * RINGS_PER_INDEX_LANE * 64 * 1024;  // 64KB per ring
    size_t detail_ring_total = MAX_THREADS * RINGS_PER_DETAIL_LANE * 256 * 1024; // 256KB per ring
    size_t ring_memory_total = index_ring_total + detail_ring_total;
    
    return struct_size + ring_memory_total;
}

}  // extern "C"