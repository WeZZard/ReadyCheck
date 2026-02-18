/*
 * BenchWorkloadObjcApp - E2E benchmark workload for Objective-C method tracing
 *                        (macOS Application bundle variant).
 *
 * Exercises seven invocation patterns:
 *   1. objc_msgSend (instance method): -[BenchWorker workLeaf:]
 *   2. Category method:               -[BenchWorker(Extra) categoryWork:]
 *   3. Protocol method:               id<BenchWorkable> -> [worker protocolWork:]
 *   4. Block invocation:              uint64_t (^workBlock)(uint64_t)
 *   5. Class method:                  +[BenchWorker classLeaf:]
 *   6. Recursive (binary tree):       -[BenchWorker treeRecurse:acc:]
 *   7. Threaded burst:                dispatch_queue with worker calls
 *
 * With Xcode builds using STRIP_STYLE=debugging, ObjC method implementations
 * are preserved as hookable symbols. No global C function wrappers needed.
 *
 * Runs in applicationDidFinishLaunching with default parameters
 * (outer=1000, inner=1000, depth=15, threads=4, thread_iterations=10000).
 * No CLI argument parsing. Prints BENCH_RESULT to stderr then terminates.
 */

#import <Cocoa/Cocoa.h>
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
 * Application delegate - runs workload on launch, then terminates
 * --------------------------------------------------------------------------- */

@interface BenchAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation BenchAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)note {
    @autoreleasepool {
        const uint64_t outer        = 1000;
        const uint64_t inner        = 1000;
        const uint64_t depth        = 15;
        const uint64_t threads      = 4;
        const uint64_t thread_iters = 10000;

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
                [NSApp terminate:nil];
                return;
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
    }

    [NSApp terminate:nil];
}

@end

/* ---------------------------------------------------------------------------
 * main - NSApplication entry point
 * --------------------------------------------------------------------------- */

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        BenchAppDelegate *delegate = [[BenchAppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
