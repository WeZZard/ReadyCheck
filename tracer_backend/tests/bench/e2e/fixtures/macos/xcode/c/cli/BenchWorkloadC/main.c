/*
 * BenchWorkloadC - E2E benchmark workload for C function tracing.
 *
 * Exercises four invocation patterns:
 *   1. Direct function call chain:  work_outer -> work_middle -> work_leaf
 *   2. Function pointer call:       work_fn_t pointer -> work_leaf
 *   3. Recursive call:              tree_recurse (binary tree)
 *   4. Threaded burst:              pthread workers calling work_leaf
 *
 * Prints BENCH_RESULT to stderr with total_calls and checksum.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/* ---------------------------------------------------------------------------
 * Hot-path functions - all noinline so the tracer can intercept them.
 * No visibility attributes: Xcode build settings handle symbol export.
 * No I/O in any hot-path function.
 * --------------------------------------------------------------------------- */

typedef uint64_t (*work_fn_t)(uint64_t);

__attribute__((noinline))
uint64_t work_leaf(uint64_t x)
{
    /* lightweight arithmetic -- no I/O */
    return x ^ (x >> 3) ^ (x << 5);
}

__attribute__((noinline))
uint64_t work_middle(uint64_t count)
{
    uint64_t acc = 0;
    for (uint64_t i = 0; i < count; i++) {
        acc += work_leaf(acc + i);
    }
    return acc;
}

__attribute__((noinline))
uint64_t work_outer(uint64_t outer, uint64_t inner)
{
    uint64_t acc = 0;
    for (uint64_t i = 0; i < outer; i++) {
        acc += work_middle(inner);
        /* Compiler barrier: clobber both acc AND inner so the compiler
           cannot prove work_middle(inner) is loop-invariant.  The asm
           doesn't actually change either value at runtime. */
        __asm__ volatile("" : "+r"(acc), "+r"(inner));
    }
    return acc;
}

/* ---------------------------------------------------------------------------
 * Recursive binary-tree traversal.
 * At each non-leaf node we recurse left and right; at leaves we call
 * work_leaf.  Total invocations of tree_recurse = 2^(depth+1) - 1.
 * --------------------------------------------------------------------------- */

__attribute__((noinline))
uint64_t tree_recurse(uint64_t depth, uint64_t acc)
{
    if (depth == 0) {
        return work_leaf(acc);
    }
    uint64_t left  = tree_recurse(depth - 1, acc + 1);
    uint64_t right = tree_recurse(depth - 1, acc + 2);
    return left ^ right;
}

/* ---------------------------------------------------------------------------
 * Threaded burst -- each thread calls work_leaf in a tight loop.
 * --------------------------------------------------------------------------- */

struct thread_args {
    uint64_t iterations;
    uint64_t checksum;   /* written by thread */
};

static void *thread_worker(void *arg)
{
    struct thread_args *ta = (struct thread_args *)arg;
    uint64_t acc = 0;
    for (uint64_t i = 0; i < ta->iterations; i++) {
        acc += work_leaf(acc + i);
    }
    ta->checksum = acc;
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Argument parsing helpers
 * --------------------------------------------------------------------------- */

static int parse_uint64(const char *s, uint64_t *out)
{
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (end == s || *end != '\0') return -1;
    *out = (uint64_t)v;
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [--outer N] [--inner N] [--depth N] "
        "[--threads N] [--thread-iterations N]\n", prog);
    exit(1);
}

/* ---------------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    uint64_t outer            = 1000;
    uint64_t inner            = 1000;
    uint64_t depth            = 15;
    uint64_t threads          = 4;
    uint64_t thread_iters     = 10000;

    for (int i = 1; i < argc; i++) {
        if (i + 1 >= argc) usage(argv[0]);

        if (strcmp(argv[i], "--outer") == 0) {
            if (parse_uint64(argv[++i], &outer) != 0) usage(argv[0]);
        } else if (strcmp(argv[i], "--inner") == 0) {
            if (parse_uint64(argv[++i], &inner) != 0) usage(argv[0]);
        } else if (strcmp(argv[i], "--depth") == 0) {
            if (parse_uint64(argv[++i], &depth) != 0) usage(argv[0]);
        } else if (strcmp(argv[i], "--threads") == 0) {
            if (parse_uint64(argv[++i], &threads) != 0) usage(argv[0]);
        } else if (strcmp(argv[i], "--thread-iterations") == 0) {
            if (parse_uint64(argv[++i], &thread_iters) != 0) usage(argv[0]);
        } else {
            usage(argv[0]);
        }
    }

    uint64_t checksum = 0;
    uint64_t total_calls = 0;

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    /* -----------------------------------------------------------------------
     * Pattern 1 - Direct call chain
     *   work_outer:  1 call
     *   work_middle: outer calls
     *   work_leaf:   outer * inner calls
     * ----------------------------------------------------------------------- */
    checksum ^= work_outer(outer, inner);
    total_calls += 1 + outer + outer * inner;

    /* -----------------------------------------------------------------------
     * Pattern 2 - Function pointer call
     *   outer calls to work_leaf via pointer
     * ----------------------------------------------------------------------- */
    {
        work_fn_t fn = work_leaf;
        uint64_t fp_acc = 0;
        for (uint64_t i = 0; i < outer; i++) {
            fp_acc += fn(fp_acc + i);
        }
        checksum ^= fp_acc;
        total_calls += outer;
    }

    /* -----------------------------------------------------------------------
     * Pattern 3 - Recursive (binary tree)
     *   tree_recurse invocations: 2^(depth+1) - 1
     *   (work_leaf calls at leaves are included in tree_recurse count
     *    since tree_recurse itself is the counted function)
     * ----------------------------------------------------------------------- */
    checksum ^= tree_recurse(depth, 0);
    {
        /* 2^(depth+1) - 1 */
        uint64_t recurse_calls = ((uint64_t)1 << (depth + 1)) - 1;
        total_calls += recurse_calls;
    }

    /* -----------------------------------------------------------------------
     * Pattern 4 - Threaded burst
     *   threads * thread_iters work_leaf calls
     * ----------------------------------------------------------------------- */
    {
        pthread_t *tids = (pthread_t *)malloc(sizeof(pthread_t) * threads);
        struct thread_args *targs =
            (struct thread_args *)malloc(sizeof(struct thread_args) * threads);

        if (!tids || !targs) {
            fprintf(stderr, "allocation failed\n");
            return 1;
        }

        for (uint64_t t = 0; t < threads; t++) {
            targs[t].iterations = thread_iters;
            targs[t].checksum   = 0;
            if (pthread_create(&tids[t], NULL, thread_worker, &targs[t]) != 0) {
                fprintf(stderr, "pthread_create failed\n");
                return 1;
            }
        }

        for (uint64_t t = 0; t < threads; t++) {
            pthread_join(tids[t], NULL);
            checksum ^= targs[t].checksum;
        }

        total_calls += threads * thread_iters;

        free(targs);
        free(tids);
    }

    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double hotpath_ms = (ts_end.tv_sec - ts_start.tv_sec) * 1000.0
                      + (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000.0;

    fprintf(stderr, "BENCH_RESULT lang=c total_calls=%llu checksum=%llu hotpath_ms=%.3f\n",
            (unsigned long long)total_calls,
            (unsigned long long)checksum,
            hotpath_ms);

    return 0;
}
