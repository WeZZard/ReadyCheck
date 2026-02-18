#ifndef BENCH_WORKLOAD_C_H
#define BENCH_WORKLOAD_C_H

#include <stdint.h>

/* Run the C benchmark workload and print BENCH_RESULT to stderr. */
void bench_workload_run(void);

/* Individual hot-path functions (noinline for tracer interception). */
uint64_t work_leaf(uint64_t x);
uint64_t work_middle(uint64_t count);
uint64_t work_outer(uint64_t outer, uint64_t inner);
uint64_t tree_recurse(uint64_t depth, uint64_t acc);

#endif /* BENCH_WORKLOAD_C_H */
