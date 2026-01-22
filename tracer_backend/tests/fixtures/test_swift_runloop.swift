// test_swift_runloop.swift
// NSRunLoop-based long-running Swift program for tracing tests
// Simulates a real app that runs continuously with periodic activity

import Foundation

class ActivityGenerator {
    private var timer: Timer?
    private var counter: Int = 0

    func start() {
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            self?.tick()
        }
    }

    func stop() {
        timer?.invalidate()
        timer = nil
    }

    private func tick() {
        counter += 1
        processData(counter)
    }

    private func processData(_ value: Int) {
        // Generate varied function calls for tracing
        let result = compute(value)
        if result % 10 == 0 {
            handleMilestone(result)
        }
    }

    private func compute(_ n: Int) -> Int {
        return n * 2 + 1
    }

    private func handleMilestone(_ n: Int) {
        // Silent milestone - don't spam console
        _ = n
    }
}

@_cdecl("swift_runloop_main")
public func swiftRunloopMain(durationSecs: Int32) -> Int32 {
    let generator = ActivityGenerator()
    generator.start()

    // Run for specified duration
    let runUntil = Date().addingTimeInterval(Double(durationSecs))
    RunLoop.current.run(until: runUntil)

    generator.stop()
    return 0
}

@main
struct TestSwiftRunLoop {
    static func main() {
        let duration: Int32 = CommandLine.arguments.count > 1
            ? Int32(CommandLine.arguments[1]) ?? 5
            : 5
        _ = swiftRunloopMain(durationSecs: duration)
    }
}
