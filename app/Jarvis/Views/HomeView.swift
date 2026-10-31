import SwiftUI

struct HomeView: View {
    @Environment(AppModel.self) private var model

    private var rooms: [(String, [HomeDevice])] {
        let visible = model.devices.filter { $0.hidden != true }
        let names = Array(Set(visible.map(\.room))).sorted()
        return names.map { name in (name, visible.filter { $0.room == name }.sorted { $0.name < $1.name }) }
    }

    var body: some View {
        NavigationStack {
            List {
                if model.devices.isEmpty {
                    Text("Žádná světla ani zásuvky. Displej musí být spárovaný s IKEA hubem.")
                        .foregroundStyle(Theme.muted)
                }
                ForEach(rooms, id: \.0) { room in
                    Section(room.0.isEmpty ? "Bez místnosti" : room.0) {
                        ForEach(room.1) { device in
                            DeviceRow(device: device)
                        }
                    }
                }
            }
            .scrollContentBackground(.hidden)
            .background(Theme.background)
            .navigationTitle("Domov")
            .navigationBarTitleDisplayMode(.inline)
            .safeAreaInset(edge: .top) { OfflineBanner() }
            .refreshable { await model.loadDevices() }
            .task {
                while !Task.isCancelled {
                    await model.loadDevices()
                    try? await Task.sleep(nanoseconds: model.remote ? 20_000_000_000 : 4_000_000_000)
                }
            }
        }
    }
}

private struct DeviceRow: View {
    @Environment(AppModel.self) private var model
    let device: HomeDevice
    @State private var level = 100.0
    @State private var sliding = false

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Toggle(isOn: Binding(get: { device.on }, set: { on in
                Task { await model.power(device, on: on) }
            })) {
                Label(device.name, systemImage: device.light ? (device.on ? "lightbulb.fill" : "lightbulb") : "poweroutlet.type.f")
                    .foregroundStyle(device.reachable ? .white : Theme.muted)
            }
            .disabled(!device.reachable)
            if device.light && device.on && device.reachable {
                Slider(value: $level, in: 1...100) { editing in
                    sliding = editing
                    if !editing {
                        Task { await model.brightness(device, level: Int(level.rounded())) }
                    }
                }
            }
        }
        .onAppear { level = Double(device.level ?? 100) }
        .onChange(of: device.level) { _, fresh in
            if !sliding, let fresh, fresh > 0 {
                level = Double(fresh)
            }
        }
    }
}
