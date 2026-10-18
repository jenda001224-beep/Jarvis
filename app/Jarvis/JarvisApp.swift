import SwiftUI

@main
struct JarvisApp: App {
    @State private var model = AppModel()
    @Environment(\.scenePhase) private var phase

    var body: some Scene {
        WindowGroup {
            RootView()
                .environment(model)
                .preferredColorScheme(.dark)
                .tint(Theme.accent)
        }
        .onChange(of: phase) { _, phase in
            if phase == .active && model.paired {
                model.startPolling()
            } else if phase != .active {
                model.stopPolling()
            }
        }
    }
}
