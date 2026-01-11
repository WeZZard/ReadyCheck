import Foundation

enum SessionMode: String, CaseIterable, Identifiable {
    case launch = "Launch"
    case attach = "Attach"

    var id: String { rawValue }
}
