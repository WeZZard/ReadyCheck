import Foundation

struct BundleItem: Identifiable, Hashable {
    let id = UUID()
    let bundlePath: String
    let startedAt: Date
    let endedAt: Date
}

struct DaemonResponse<T: Decodable>: Decodable {
    let ok: Bool
    let error: String?
    let data: T?
}

struct SessionInfo: Decodable {
    let session_root: String
    let trace_root: String
    let trace_session: String?
    let is_voice_active: Bool
}

struct StatusInfo: Decodable {
    let is_session_active: Bool
    let is_voice_active: Bool
    let session_root: String?
    let trace_root: String?
    let trace_session: String?
}

struct VoiceStartInfo: Decodable {
    let is_session_active: Bool
    let is_voice_active: Bool
    let session_root: String
    let trace_root: String
    let trace_session: String?
    let segment_dir: String
    let voice_path: String
}

struct BundleInfo: Decodable {
    let bundle_path: String
    let trace_session: String?
}
