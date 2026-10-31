import SwiftUI

struct RootView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        Group {
            if model.paired {
                TabView {
                    ChatView()
                        .tabItem { Label("Jarvis", systemImage: "bubble.left.and.text.bubble.right") }
                    HomeView()
                        .tabItem { Label("Domov", systemImage: "house") }
                    TodosView()
                        .tabItem { Label("Úkoly", systemImage: "checklist") }
                    AlarmsView()
                        .tabItem { Label("Budíky", systemImage: "alarm") }
                    DeviceView()
                        .tabItem { Label("Zařízení", systemImage: "display") }
                        .badge(model.unread)
                }
                .task { model.startPolling() }
            } else {
                PairingView()
            }
        }
        .background(Theme.background.ignoresSafeArea())
    }
}
