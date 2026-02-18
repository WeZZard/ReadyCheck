// BenchWorkloadSwiftApp.swift - SwiftUI App E2E benchmark workload for ADA tracing overhead measurement
//
// Exercises seven invocation patterns using native Swift dispatch:
//   1. Static dispatch (struct)       - no vtable, direct call
//   2. V-table dispatch (class)       - class vtable interception
//   3. Protocol witness table          - existential call through `any BenchWorkable`
//   4. @objc dynamic dispatch          - ObjC-compatible Swift via NSObject subclass
//   5. Closure call                    - Swift closure hooking
//   6. Recursive (binary tree)         - treeRecurse invocations
//   7. Threaded burst                  - DispatchQueue.concurrentPerform with worker calls
//
// This is the SwiftUI App bundle variant. It runs the same workload as the CLI version
// but packaged as a macOS application bundle (.app). The app runs the workload on appear
// and exits immediately.
//
// Prints BENCH_RESULT to stderr with total_calls and checksum.

import SwiftUI
import Foundation

// MARK: - Compiler Barrier

/// Prevents loop-invariant code motion (LICM) by forcing both the accumulator
/// and the loop argument through opaque memory operations.  This is the Swift
/// equivalent of `__asm__ volatile("" : "+r"(acc), "+r"(arg))` in C/C++.
/// Both values are written back unchanged, but the compiler cannot prove that.
@inline(never)
private func _benchBarrier(_ acc: inout UInt64, _ arg: inout Int) {
    withUnsafeMutablePointer(to: &acc) { p in p.pointee = p.pointee }
    withUnsafeMutablePointer(to: &arg) { p in p.pointee = p.pointee }
}

// MARK: - Static Dispatch (struct)

struct StaticWorker {
    @inline(never)
    func workLeaf(_ x: UInt64) -> UInt64 {
        var x = x
        x ^= x >> 33
        x &*= 0xff51afd7ed558ccd
        x ^= x >> 33
        x &*= 0xc4ceb9fe1a85ec53
        x ^= x >> 33
        return x
    }

    @inline(never)
    func workMiddle(count: Int) -> UInt64 {
        var acc: UInt64 = 0
        for i in 0..<count {
            acc &+= workLeaf(acc ^ UInt64(i))
        }
        return acc
    }
}

// MARK: - V-table Dispatch (class)

class VTableWorker {
    @inline(never)
    func workLeaf(_ x: UInt64) -> UInt64 {
        var x = x
        x ^= x >> 33
        x &*= 0xff51afd7ed558ccd
        x ^= x >> 33
        x &*= 0xc4ceb9fe1a85ec53
        x ^= x >> 33
        return x
    }

    @inline(never)
    func workMiddle(count: Int) -> UInt64 {
        var acc: UInt64 = 0
        for i in 0..<count {
            acc &+= workLeaf(acc ^ UInt64(i))
        }
        return acc
    }

    @inline(never)
    func workOuter(outer: Int, inner: Int) -> UInt64 {
        var acc: UInt64 = 0
        var arg = inner
        for _ in 0..<outer {
            acc &+= workMiddle(count: arg)
            _benchBarrier(&acc, &arg)  // prevent LICM of workMiddle
        }
        return acc
    }
}

// MARK: - Protocol Witness Table

protocol BenchWorkable {
    func workLeaf(_ x: UInt64) -> UInt64
    func workMiddle(count: Int) -> UInt64
}

extension VTableWorker: BenchWorkable {}

// MARK: - @objc Dynamic Dispatch

class ObjCWorker: NSObject {
    @objc dynamic func workLeaf(_ x: UInt64) -> UInt64 {
        var x = x
        x ^= x >> 33
        x &*= 0xff51afd7ed558ccd
        x ^= x >> 33
        x &*= 0xc4ceb9fe1a85ec53
        x ^= x >> 33
        return x
    }

    @objc dynamic func workMiddle(count: Int) -> UInt64 {
        var acc: UInt64 = 0
        for i in 0..<count {
            acc &+= workLeaf(acc ^ UInt64(i))
        }
        return acc
    }
}

// MARK: - Recursive Function

@inline(never)
func treeRecurse(depth: Int, acc: UInt64) -> UInt64 {
    if depth <= 0 {
        var x = acc
        x ^= x >> 33
        x &*= 0xff51afd7ed558ccd
        x ^= x >> 33
        x &*= 0xc4ceb9fe1a85ec53
        x ^= x >> 33
        return x
    }
    let left = treeRecurse(depth: depth - 1, acc: acc &+ 1)
    let right = treeRecurse(depth: depth - 1, acc: acc &+ 2)
    return left ^ right
}

// MARK: - Workload Runner

func runBenchWorkload() {
    // Default parameters (same as CLI defaults)
    let outer = 1000
    let inner = 1000
    let depth = 15
    let threads = 4
    let threadIterations = 10000

    var checksum: UInt64 = 0
    var totalCalls: UInt64 = 0

    let hotpathStart = DispatchTime.now()

    // ---------------------------------------------------------------------
    // Pattern 1 - Static dispatch (struct, flat loop)
    //   workMiddle: outer calls
    //   workLeaf:   outer * inner calls
    //   Total:      outer + outer * inner
    // ---------------------------------------------------------------------
    do {
        let worker = StaticWorker()
        var acc: UInt64 = 0
        var arg = inner
        for _ in 0..<outer {
            acc &+= worker.workMiddle(count: arg)
            _benchBarrier(&acc, &arg)  // prevent LICM of workMiddle
        }
        checksum ^= acc
        totalCalls += UInt64(outer) + UInt64(outer) &* UInt64(inner)
    }

    // ---------------------------------------------------------------------
    // Pattern 2 - V-table dispatch (class)
    //   workOuter:  1 call
    //   workMiddle: outer calls
    //   workLeaf:   outer * inner calls
    //   Total:      1 + outer + outer * inner
    // ---------------------------------------------------------------------
    do {
        let worker = VTableWorker()
        checksum ^= worker.workOuter(outer: outer, inner: inner)
        totalCalls += 1 + UInt64(outer) + UInt64(outer) &* UInt64(inner)
    }

    // ---------------------------------------------------------------------
    // Pattern 3 - Protocol witness table
    //   Cast VTableWorker as `any BenchWorkable`, call workLeaf via
    //   protocol witness table dispatch.
    //   Total: outer calls
    // ---------------------------------------------------------------------
    do {
        let witness: any BenchWorkable = VTableWorker()
        var acc: UInt64 = 0
        for i in 0..<outer {
            acc &+= witness.workLeaf(acc ^ UInt64(i))
        }
        checksum ^= acc
        totalCalls += UInt64(outer)
    }

    // ---------------------------------------------------------------------
    // Pattern 4 - @objc dynamic dispatch
    //   ObjC message send via objc_msgSend.
    //   Total: outer calls
    // ---------------------------------------------------------------------
    do {
        let worker = ObjCWorker()
        var acc: UInt64 = 0
        for i in 0..<outer {
            acc &+= worker.workLeaf(acc ^ UInt64(i))
        }
        checksum ^= acc
        totalCalls += UInt64(outer)
    }

    // ---------------------------------------------------------------------
    // Pattern 5 - Closure call
    //   Swift closure captured and invoked in a loop.
    //   Total: outer calls
    // ---------------------------------------------------------------------
    do {
        let closure: (UInt64) -> UInt64 = { x in
            var x = x
            x ^= x >> 33
            x &*= 0xff51afd7ed558ccd
            x ^= x >> 33
            x &*= 0xc4ceb9fe1a85ec53
            x ^= x >> 33
            return x
        }
        var acc: UInt64 = 0
        for i in 0..<outer {
            acc &+= closure(acc ^ UInt64(i))
        }
        checksum ^= acc
        totalCalls += UInt64(outer)
    }

    // ---------------------------------------------------------------------
    // Pattern 6 - Recursive (binary tree)
    //   treeRecurse invocations: 2^(depth+1) - 1
    // ---------------------------------------------------------------------
    do {
        checksum ^= treeRecurse(depth: depth, acc: 0)
        let recurseCalls: UInt64 = (UInt64(1) << (depth + 1)) &- 1
        totalCalls += recurseCalls
    }

    // ---------------------------------------------------------------------
    // Pattern 7 - Threaded burst
    //   threads * threadIterations calls to StaticWorker.workLeaf
    // ---------------------------------------------------------------------
    do {
        let staticWorker = StaticWorker()
        let lock = NSLock()
        var threadResults = [UInt64](repeating: 0, count: threads)

        DispatchQueue.concurrentPerform(iterations: threads) { threadId in
            var acc: UInt64 = 0
            for i in 0..<threadIterations {
                acc &+= staticWorker.workLeaf(acc ^ UInt64(i))
            }
            lock.lock()
            threadResults[threadId] = acc
            lock.unlock()
        }

        for r in threadResults {
            checksum ^= r
        }
        totalCalls += UInt64(threads) &* UInt64(threadIterations)
    }

    let hotpathEnd = DispatchTime.now()
    let hotpathNs = hotpathEnd.uptimeNanoseconds - hotpathStart.uptimeNanoseconds
    let hotpathMs = Double(hotpathNs) / 1_000_000.0

    fputs("BENCH_RESULT lang=swift total_calls=\(totalCalls) checksum=\(checksum) hotpath_ms=\(String(format: "%.3f", hotpathMs))\n", stderr)
}

// MARK: - App Entry Point

@main
struct BenchWorkloadSwiftApp: App {
    var body: some Scene {
        WindowGroup {
            Color.clear.onAppear {
                runBenchWorkload()
                exit(0)
            }
        }
    }
}
