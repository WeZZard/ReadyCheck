import SwiftUI

struct ContentView: View {
    @StateObject private var viewModel = CaptureViewModel()

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("ADA Multimodal Capture")
                .font(.title2)

            GroupBox(label: Text("Target")) {
                VStack(alignment: .leading, spacing: 8) {
                    Picker("Mode", selection: $viewModel.mode) {
                        ForEach(SessionMode.allCases) { mode in
                            Text(mode.rawValue).tag(mode)
                        }
                    }
                    .pickerStyle(.segmented)

                    if viewModel.mode == .launch {
                        HStack {
                            TextField("/path/to/App.app or binary", text: $viewModel.targetPath)
                            Button("Browse") { viewModel.chooseTarget() }
                        }
                        TextField("Args", text: $viewModel.argsText)
                    } else {
                        TextField("PID", text: $viewModel.pidText)
                            .textFieldStyle(.roundedBorder)
                    }
                }
            }

            GroupBox(label: Text("Output")) {
                HStack {
                    TextField("/path/to/captures", text: $viewModel.outputPath)
                    Button("Browse") { viewModel.chooseOutput() }
                }
            }

            GroupBox(label: Text("Session")) {
                HStack {
                    Button(viewModel.isSessionActive ? "Stop Session" : "Start Session") {
                        if viewModel.isSessionActive {
                            viewModel.stopSession()
                        } else {
                            viewModel.startSession()
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled((viewModel.mode == .launch && viewModel.targetPath.isEmpty) ||
                              (viewModel.mode == .attach && viewModel.pidText.isEmpty))

                    Button(viewModel.isVoiceActive ? "Stop Voice" : "Start Voice") {
                        viewModel.toggleVoice()
                    }
                    .disabled(!viewModel.isSessionActive)
                }
                if let trace = viewModel.traceSessionPath {
                    Button("Reveal Trace") {
                        viewModel.revealTrace()
                    }
                    .padding(.top, 4)
                    Text(trace)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }

            GroupBox(label: Text("Bundles")) {
                if viewModel.bundles.isEmpty {
                    Text("No bundles yet")
                        .foregroundColor(.secondary)
                } else {
                    List(viewModel.bundles) { item in
                        VStack(alignment: .leading, spacing: 4) {
                            Text(item.bundlePath)
                                .font(.caption)
                                .lineLimit(2)
                            Text("\(item.startedAt.formatted()) → \(item.endedAt.formatted())")
                                .font(.caption2)
                                .foregroundColor(.secondary)
                            Button("Reveal Bundle") {
                                viewModel.revealBundle(item)
                            }
                        }
                    }
                    .frame(height: 160)
                }
            }

            Text(viewModel.statusMessage)
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .padding(16)
        .frame(minWidth: 420, minHeight: 640)
    }
}
