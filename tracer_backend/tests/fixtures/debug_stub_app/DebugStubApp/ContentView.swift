import SwiftUI

struct ContentView: View {
    @State private var scale: CGFloat = 1.0
    @State private var counter: Int = 0

    var body: some View {
        VStack(spacing: 20) {
            Text("Debug Stub Test")
                .font(.largeTitle)
                .scaleEffect(scale)

            Text("Counter: \(counter)")
                .font(.title2)

            Button("Increment") {
                incrementCounter()
            }
            .buttonStyle(.borderedProminent)
        }
        .padding()
        .onAppear {
            startAnimation()
            scheduleExit()
        }
    }

    private func startAnimation() {
        withAnimation(.easeInOut(duration: 1.0).repeatForever(autoreverses: true)) {
            scale = 1.2
        }
    }

    private func incrementCounter() {
        counter += 1
        performWork()
    }

    private func performWork() {
        // Do some work to generate traceable function calls
        for i in 0..<10 {
            _ = computeValue(i)
        }
    }

    private func computeValue(_ input: Int) -> Int {
        return input * input + input
    }

    private func scheduleExit() {
        // Exit after brief run for automated testing
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
            // Perform some final work
            for _ in 0..<5 {
                incrementCounter()
            }
            exit(0)
        }
    }
}

#Preview {
    ContentView()
}
