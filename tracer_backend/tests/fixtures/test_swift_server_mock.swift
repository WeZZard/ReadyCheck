// test_swift_server_mock.swift
// Server-like Swift program for tracing tests
// Simulates a WebDAV-style server with request processing

import Foundation

protocol RequestHandler {
    func handle(request: Data) -> Data
}

class MockServer {
    private var isRunning = false
    private let queue = DispatchQueue(label: "mock.server")
    private var requestCount = 0

    func start() {
        isRunning = true
        scheduleNextRequest()
    }

    func stop() {
        isRunning = false
    }

    private func scheduleNextRequest() {
        guard isRunning else { return }
        queue.asyncAfter(deadline: .now() + 0.05) { [weak self] in
            self?.simulateRequest()
            self?.scheduleNextRequest()
        }
    }

    private func simulateRequest() {
        requestCount += 1
        let request = generateRequest(id: requestCount)
        let response = processRequest(request)
        logResponse(response)
    }

    private func generateRequest(id: Int) -> Data {
        return "GET /resource/\(id)".data(using: .utf8)!
    }

    private func processRequest(_ data: Data) -> Data {
        // Simulate processing
        let str = String(data: data, encoding: .utf8) ?? ""
        return "200 OK: \(str)".data(using: .utf8)!
    }

    private func logResponse(_ data: Data) {
        // Silent logging to avoid console spam
        _ = data.count
    }
}

@_cdecl("swift_server_mock_main")
public func swiftServerMockMain(durationSecs: Int32) -> Int32 {
    let server = MockServer()
    server.start()

    let runUntil = Date().addingTimeInterval(Double(durationSecs))
    while Date() < runUntil {
        RunLoop.current.run(until: Date().addingTimeInterval(0.1))
    }

    server.stop()
    return 0
}

@main
struct TestSwiftServerMock {
    static func main() {
        let duration: Int32 = CommandLine.arguments.count > 1
            ? Int32(CommandLine.arguments[1]) ?? 5
            : 5
        _ = swiftServerMockMain(durationSecs: duration)
    }
}
