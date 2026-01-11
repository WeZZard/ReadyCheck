import Foundation

final class DaemonClient {
    private let process: Process
    private let stdinHandle: FileHandle
    private let stdoutHandle: FileHandle
    private let queue = DispatchQueue(label: "ada.capture.daemon")

    init() throws {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: DaemonClient.resolveDaemonPath())
        process.arguments = []

        let stdinPipe = Pipe()
        let stdoutPipe = Pipe()

        process.standardInput = stdinPipe
        process.standardOutput = stdoutPipe
        process.standardError = FileHandle.standardError

        try process.run()

        self.process = process
        self.stdinHandle = stdinPipe.fileHandleForWriting
        self.stdoutHandle = stdoutPipe.fileHandleForReading
    }

    deinit {
        if process.isRunning {
            process.terminate()
        }
    }

    func startSession(binary: String?, pid: Int?, args: [String], output: String?) throws -> SessionInfo {
        let command = StartSessionCommand(binary: binary, pid: pid, args: args, output: output)
        let response: DaemonResponse<SessionInfo> = try send(command)
        return try unwrap(response)
    }

    func stopSession() throws -> StatusInfo {
        let command = SimpleCommand(cmd: "stop_session")
        let response: DaemonResponse<StatusInfo> = try send(command)
        return try unwrap(response)
    }

    func startVoice() throws -> VoiceStartInfo {
        let command = StartVoiceCommand(audio_device: nil)
        let response: DaemonResponse<VoiceStartInfo> = try send(command)
        return try unwrap(response)
    }

    func stopVoice() throws -> BundleInfo {
        let command = SimpleCommand(cmd: "stop_voice")
        let response: DaemonResponse<BundleInfo> = try send(command)
        return try unwrap(response)
    }

    func status() throws -> StatusInfo {
        let command = SimpleCommand(cmd: "status")
        let response: DaemonResponse<StatusInfo> = try send(command)
        return try unwrap(response)
    }

    private func send<T: Encodable, R: Decodable>(_ command: T) throws -> DaemonResponse<R> {
        try queue.sync {
            let payload = try JSONEncoder().encode(command)
            stdinHandle.write(payload)
            stdinHandle.write(Data([0x0A]))

            let line = try readJSONLine()
            let data = Data(line.utf8)
            return try JSONDecoder().decode(DaemonResponse<R>.self, from: data)
        }
    }

    private func unwrap<T>(_ response: DaemonResponse<T>) throws -> T {
        if response.ok, let data = response.data {
            return data
        }
        throw DaemonError.remoteError(response.error ?? "Unknown daemon error")
    }

    private func readLine() throws -> String {
        var buffer = Data()
        while true {
            let chunk = stdoutHandle.readData(ofLength: 1)
            if chunk.isEmpty {
                throw DaemonError.disconnected
            }
            if chunk[0] == 0x0A {
                break
            }
            buffer.append(chunk)
        }
        guard let line = String(data: buffer, encoding: .utf8) else {
            throw DaemonError.invalidResponse
        }
        return line
    }

    private func readJSONLine() throws -> String {
        while true {
            let line = try readLine()
            if let json = extractJSON(line) {
                return json
            }
        }
    }

    private func extractJSON(_ line: String) -> String? {
        let prefix = "ADA_JSON:"
        guard line.hasPrefix(prefix) else { return nil }
        return String(line.dropFirst(prefix.count))
    }

    private static func resolveDaemonPath() -> String {
        let bundlePath = Bundle.main.bundleURL
            .appendingPathComponent("Contents/MacOS/ada-capture-daemon")
            .path
        if FileManager.default.fileExists(atPath: bundlePath) {
            return bundlePath
        }

        if let fromPath = findInPath("ada-capture-daemon") {
            return fromPath
        }

        return "ada-capture-daemon"
    }

    private static func findInPath(_ executable: String) -> String? {
        let envPath = ProcessInfo.processInfo.environment["PATH"] ?? ""
        for path in envPath.split(separator: ":") {
            let candidate = URL(fileURLWithPath: String(path))
                .appendingPathComponent(executable)
                .path
            if FileManager.default.isExecutableFile(atPath: candidate) {
                return candidate
            }
        }
        return nil
    }
}

private struct StartSessionCommand: Encodable {
    let cmd = "start_session"
    let binary: String?
    let pid: Int?
    let args: [String]
    let output: String?
}

private struct SimpleCommand: Encodable {
    let cmd: String
}

private struct StartVoiceCommand: Encodable {
    let cmd = "start_voice"
    let audio_device: String?
}

enum DaemonError: Error {
    case disconnected
    case invalidResponse
    case remoteError(String)
}
