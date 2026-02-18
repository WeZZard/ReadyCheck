/*
 * BenchWorkloadObjc - E2E benchmark workload for Objective-C method tracing.
 *
 * Exercises six invocation patterns:
 *   1. objc_msgSend (instance method): -[BenchWorker workLeaf:]
 *   2. Category method:               -[BenchWorker(Extra) categoryWork:]
 *   3. Protocol method:               id<BenchWorkable> -> [worker protocolWork:]
 *   4. Block invocation:              uint64_t (^workBlock)(uint64_t)
 *   5. Class method:                  +[BenchWorker classLeaf:]
 *   6. Threaded burst:                dispatch_queue with worker calls
 *
 * With Xcode builds using STRIP_STYLE=debugging, ObjC method implementations
 * are preserved as hookable symbols. No global C function wrappers needed.
 *
 * Prints BENCH_RESULT to stderr with total_calls and checksum.
 */

#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
#import <time.h>

/* ---------------------------------------------------------------------------
 * Protocol
 * --------------------------------------------------------------------------- */

@protocol BenchWorkable <NSObject>
- (uint64_t)protocolWork:(uint64_t)x;
@end

/* ---------------------------------------------------------------------------
 * BenchWorker interface
 * --------------------------------------------------------------------------- */

@interface BenchWorker : NSObject <BenchWorkable>
- (uint64_t)workLeaf:(uint64_t)x;
- (uint64_t)workMiddle:(int)count;
- (uint64_t)workOuter:(int)outer inner:(int)inner;
- (uint64_t)treeRecurse:(int)depth acc:(uint64_t)acc;
+ (uint64_t)classLeaf:(uint64_t)x;
@end

/* ---------------------------------------------------------------------------
 * Category interface
 * --------------------------------------------------------------------------- */

@interface BenchWorker (Extra)
- (uint64_t)categoryWork:(uint64_t)x;
@end

/* ---------------------------------------------------------------------------
 * BenchWorker implementation
 * --------------------------------------------------------------------------- */

@implementation BenchWorker

- (uint64_t)workLeaf:(uint64_t)x {
    /* MurmurHash3 finalizer -- lightweight, no I/O */
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

- (uint64_t)workMiddle:(int)count {
    uint64_t acc = 0;
    for (int i = 0; i < count; i++) {
        acc += [self workLeaf:acc + (uint64_t)i];
    }
    return acc;
}

- (uint64_t)workOuter:(int)outer inner:(int)inner {
    uint64_t acc = 0;
    for (int i = 0; i < outer; i++) {
        acc += [self workMiddle:inner];
    }
    return acc;
}

- (uint64_t)treeRecurse:(int)depth acc:(uint64_t)acc {
    if (depth == 0) {
        return [self workLeaf:acc];
    }
    uint64_t left  = [self treeRecurse:depth - 1 acc:acc + 1];
    uint64_t right = [self treeRecurse:depth - 1 acc:acc + 2];
    return left ^ right;
}

+ (uint64_t)classLeaf:(uint64_t)x {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

- (uint64_t)protocolWork:(uint64_t)x {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

@end

/* ---------------------------------------------------------------------------
 * Category implementation
 * --------------------------------------------------------------------------- */

@implementation BenchWorker (Extra)

- (uint64_t)categoryWork:(uint64_t)x {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

@end

/* ---------------------------------------------------------------------------
 * Argument parsing helpers
 * --------------------------------------------------------------------------- */

static int parse_uint64(const char *s, uint64_t *out) {
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (end == s || *end != '\0') return -1;
    *out = (uint64_t)v;
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--outer N] [--inner N] [--depth N] "
        "[--threads N] [--thread-iterations N]\n", prog);
    exit(1);
}

/* ---------------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------------- */

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        uint64_t outer        = 1000;
        uint64_t inner        = 1000;
        uint64_t depth        = 15;
        uint64_t threads      = 4;
        uint64_t thread_iters = 10000;

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

        BenchWorker *worker = [[BenchWorker alloc] init];
        uint64_t checksum = 0;
        uint64_t total_calls = 0;

        struct timespec ts_start, ts_end;
        clock_gettime(CLOCK_MONOTONIC, &ts_start);

        /* -------------------------------------------------------------------
         * Pattern 1 - Instance method dispatch (flat loop)
         *   workOuter:inner:  1 call
         *   workMiddle:       outer calls
         *   workLeaf:         outer * inner calls
         * ------------------------------------------------------------------- */
        checksum ^= [worker workOuter:(int)outer inner:(int)inner];
        total_calls += 1 + outer + outer * inner;

        /* -------------------------------------------------------------------
         * Pattern 2 - Category method
         *   outer calls via -[BenchWorker(Extra) categoryWork:]
         * ------------------------------------------------------------------- */
        {
            uint64_t cat_acc = 0;
            for (uint64_t i = 0; i < outer; i++) {
                cat_acc += [worker categoryWork:cat_acc + i];
            }
            checksum ^= cat_acc;
            total_calls += outer;
        }

        /* -------------------------------------------------------------------
         * Pattern 3 - Protocol method
         *   outer calls via id<BenchWorkable> -> [worker protocolWork:]
         * ------------------------------------------------------------------- */
        {
            id<BenchWorkable> proto_worker = worker;
            uint64_t proto_acc = 0;
            for (uint64_t i = 0; i < outer; i++) {
                proto_acc += [proto_worker protocolWork:proto_acc + i];
            }
            checksum ^= proto_acc;
            total_calls += outer;
        }

        /* -------------------------------------------------------------------
         * Pattern 4 - Block invocation
         *   outer block calls
         * ------------------------------------------------------------------- */
        {
            uint64_t (^workBlock)(uint64_t) = ^uint64_t(uint64_t x) {
                x ^= x >> 33;
                x *= 0xff51afd7ed558ccdULL;
                x ^= x >> 33;
                x *= 0xc4ceb9fe1a85ec53ULL;
                x ^= x >> 33;
                return x;
            };
            uint64_t blk_acc = 0;
            for (uint64_t i = 0; i < outer; i++) {
                blk_acc += workBlock(blk_acc + i);
            }
            checksum ^= blk_acc;
            total_calls += outer;
        }

        /* -------------------------------------------------------------------
         * Pattern 5 - Class method
         *   outer calls via +[BenchWorker classLeaf:]
         * ------------------------------------------------------------------- */
        {
            uint64_t cls_acc = 0;
            for (uint64_t i = 0; i < outer; i++) {
                cls_acc += [BenchWorker classLeaf:cls_acc + i];
            }
            checksum ^= cls_acc;
            total_calls += outer;
        }

        /* -------------------------------------------------------------------
         * Pattern 6 (interleaved) - Recursive (binary tree)
         *   treeRecurse invocations: 2^(depth+1) - 1
         * ------------------------------------------------------------------- */
        checksum ^= [worker treeRecurse:(int)depth acc:0];
        {
            uint64_t recurse_calls = ((uint64_t)1 << (depth + 1)) - 1;
            total_calls += recurse_calls;
        }

        /* -------------------------------------------------------------------
         * Pattern 7 - Threaded burst
         *   threads * thread_iters workLeaf: calls on separate BenchWorker
         *   per thread via dispatch_queue
         * ------------------------------------------------------------------- */
        {
            dispatch_group_t group = dispatch_group_create();
            dispatch_queue_t queue = dispatch_queue_create(
                "com.bench.worker", DISPATCH_QUEUE_CONCURRENT);

            __block uint64_t *thread_checksums =
                (uint64_t *)calloc((size_t)threads, sizeof(uint64_t));
            if (!thread_checksums) {
                fprintf(stderr, "allocation failed\n");
                return 1;
            }

            for (uint64_t t = 0; t < threads; t++) {
                const uint64_t tid = t;
                const uint64_t iters = thread_iters;
                dispatch_group_async(group, queue, ^{
                    BenchWorker *tw = [[BenchWorker alloc] init];
                    uint64_t acc = 0;
                    for (uint64_t i = 0; i < iters; i++) {
                        acc += [tw workLeaf:acc + i];
                    }
                    thread_checksums[tid] = acc;
                });
            }

            dispatch_group_wait(group, DISPATCH_TIME_FOREVER);

            for (uint64_t t = 0; t < threads; t++) {
                checksum ^= thread_checksums[t];
            }

            total_calls += threads * thread_iters;

            free(thread_checksums);
        }

        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        double hotpath_ms = (ts_end.tv_sec - ts_start.tv_sec) * 1000.0
                          + (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000.0;

        fprintf(stderr, "BENCH_RESULT lang=objc total_calls=%llu checksum=%llu hotpath_ms=%.3f\n",
                (unsigned long long)total_calls,
                (unsigned long long)checksum,
                hotpath_ms);

        return 0;
    }
}
