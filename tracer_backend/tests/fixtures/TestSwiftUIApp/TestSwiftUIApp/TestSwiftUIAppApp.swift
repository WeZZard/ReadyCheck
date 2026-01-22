// TestSwiftUIAppApp.swift
// SwiftUI GUI application built with Xcode toolchain for end-to-end testing
// This fixture replicates the symbol visibility of real-world Xcode-built apps
// where most Swift symbols are local (non-exported) and only main is exported.

import SwiftUI
import Combine

// MARK: - Observable Model with @Published properties

class CounterModel: ObservableObject {
    @Published var count: Int = 0
    @Published var history: [Int] = []
    @Published var status: String = "Ready"

    private var cancellables = Set<AnyCancellable>()
    private var timer: Timer?

    init() {
        setupCombineChain()
    }

    // MARK: - Combine publisher chain

    private func setupCombineChain() {
        // Chain: count changes -> map -> filter -> sink
        $count
            .map { value in
                value * 2
            }
            .filter { $0 > 0 }
            .sink { [weak self] doubled in
                self?.handleDoubledValue(doubled)
            }
            .store(in: &cancellables)

        // History tracking via Combine
        $count
            .dropFirst()
            .collect(5)
            .sink { [weak self] batch in
                self?.processBatch(batch)
            }
            .store(in: &cancellables)
    }

    private func handleDoubledValue(_ value: Int) {
        // Silent processing - avoid console spam
        _ = value
    }

    private func processBatch(_ batch: [Int]) {
        // Process collected values
        _ = batch.reduce(0, +)
    }

    // MARK: - Timer-based activity

    func startAutoIncrement() {
        status = "Running"
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            self?.tick()
        }
    }

    func stopAutoIncrement() {
        timer?.invalidate()
        timer = nil
        status = "Stopped"
    }

    private func tick() {
        count += 1
        history.append(count)
        if history.count > 100 {
            history.removeFirst()
        }

        // Periodic async work
        if count % 10 == 0 {
            Task {
                await performAsyncWork()
            }
        }
    }

    // MARK: - async/await function

    func performAsyncWork() async {
        // Simulate async computation
        let result = await computeAsync(count)
        await MainActor.run {
            status = "Computed: \(result)"
        }
    }

    private func computeAsync(_ n: Int) async -> Int {
        // Async computation with suspension point
        try? await Task.sleep(nanoseconds: 1_000_000) // 1ms
        return n * n
    }

    func increment() {
        count += 1
        history.append(count)
    }

    func decrement() {
        count -= 1
        history.append(count)
    }

    func reset() {
        count = 0
        history.removeAll()
        status = "Reset"
    }
}

// MARK: - SwiftUI Views

struct CounterView: View {
    @StateObject private var model = CounterModel()

    var body: some View {
        VStack(spacing: 20) {
            // Status display
            Text(model.status)
                .font(.caption)
                .foregroundColor(.secondary)

            // Counter display
            Text("\(model.count)")
                .font(.system(size: 72, weight: .bold, design: .rounded))

            // Manual controls
            HStack(spacing: 20) {
                Button(action: { model.decrement() }) {
                    Image(systemName: "minus.circle.fill")
                        .font(.largeTitle)
                }

                Button(action: { model.increment() }) {
                    Image(systemName: "plus.circle.fill")
                        .font(.largeTitle)
                }
            }

            // Auto-increment controls
            HStack(spacing: 20) {
                Button("Start Auto") {
                    model.startAutoIncrement()
                }
                .buttonStyle(.borderedProminent)

                Button("Stop") {
                    model.stopAutoIncrement()
                }
                .buttonStyle(.bordered)

                Button("Reset") {
                    model.reset()
                }
                .buttonStyle(.bordered)
            }

            // Async button
            Button("Async Work") {
                Task {
                    await model.performAsyncWork()
                }
            }
            .buttonStyle(.bordered)

            // History list
            Text("History (last 10)")
                .font(.headline)
                .padding(.top)

            List(model.history.suffix(10), id: \.self) { value in
                HStack {
                    Text("Value:")
                    Spacer()
                    Text("\(value)")
                        .fontWeight(.medium)
                }
            }
            .frame(height: 200)
        }
        .padding()
        .frame(minWidth: 300, minHeight: 500)
        .onAppear {
            // Start auto-increment on launch for automated testing
            model.startAutoIncrement()
        }
    }
}

// MARK: - App Entry Point

@main
struct TestSwiftUIApp: App {
    @State private var shouldQuit = false

    init() {
        // Handle gtest discovery - exit cleanly when build system probes for tests
        // This prevents the GUI from launching during cargo build
        for arg in CommandLine.arguments {
            if arg.hasPrefix("--gtest") {
                // Not a gtest executable - exit with non-zero to indicate no tests
                exit(1)
            }
        }
    }

    var body: some Scene {
        WindowGroup {
            CounterView()
                .onReceive(NotificationCenter.default.publisher(for: NSApplication.willTerminateNotification)) { _ in
                    // Clean shutdown handling
                    shouldQuit = true
                }
        }
        .windowResizability(.contentSize)
    }
}
