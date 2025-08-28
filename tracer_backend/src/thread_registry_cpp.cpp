// thread_registry_cpp.cpp - C++ implementation with C compatibility layer

#include "thread_registry_cpp.hpp"
#include <cstdlib>
#include <cstring>
#include <cassert>

namespace ada {

// Static TLS for fast path
thread_local ThreadLaneSet* tls_my_lanes_cpp = nullptr;

}  // namespace ada

// =============================================================================
// C Compatibility Layer - Wraps C++ implementation for C interface
// =============================================================================

extern "C" {

#include "thread_registry.h"

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

ThreadLaneSet* thread_registry_register(ThreadRegistry* registry, uint32_t thread_id) {
    if (!registry) return nullptr;
    auto* cpp_registry = reinterpret_cast<ada::ThreadRegistryCpp*>(registry);
    auto* cpp_lanes = cpp_registry->register_thread(thread_id);
    return reinterpret_cast<ThreadLaneSet*>(cpp_lanes);
}

ThreadLaneSet* thread_registry_get_thread_lanes(ThreadRegistry* registry, uint32_t thread_id) {
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
    return reinterpret_cast<ThreadLaneSet*>(ada::tls_my_lanes_cpp);
}

void thread_registry_set_my_lanes(ThreadLaneSet* lanes) {
    ada::tls_my_lanes_cpp = reinterpret_cast<ada::ThreadLaneSetCpp*>(lanes);
}

bool thread_registry_unregister(ThreadRegistry* registry, uint32_t thread_id) {
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

RingBuffer* lane_get_active_ring(Lane* lane) {
    if (!lane) return nullptr;
    auto* cpp_lane = reinterpret_cast<ada::LaneCpp*>(lane);
    return cpp_lane->get_active_ring();
}

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
    return ada::ThreadRegistryCpp::totalSizeNeeded(MAX_THREADS);
}

}  // extern "C"