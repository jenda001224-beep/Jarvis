import SwiftUI

struct RootView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        Group {
            if model.paired {
                TabView {
                    TodosView()
                        .tabItem { Label("Úkoly", systemImage: "checklist") }
                    AlarmsView()
                        .tabItem { Label("Budíky", systemImage: "alarm") }
                    DeviceView()
                        .tabItem { Label("Zařízení", systemImage: "display") }
                }
                .task { model.startPolling() }
            } else {
                PairingView()
            }
        }
        .background(Theme.background.ignoresSafeArea())
    }
}
