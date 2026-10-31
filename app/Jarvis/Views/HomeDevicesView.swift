import SwiftUI

struct HomeDevicesView: View {
    @Environment(AppModel.self) private var model
    @State private var editing: HomeDevice?

    var body: some View {
        List {
            Section {
                ForEach(model.devices) { device in
                    Button {
                        editing = device
                    } label: {
                        HStack(spacing: 12) {
                            Image(systemName: device.light ? (device.on ? "lightbulb.fill" : "lightbulb") : "poweroutlet.type.f")
                                .foregroundStyle(device.on ? Theme.accent : Theme.muted)
                                .frame(width: 24)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(device.name)
                                    .foregroundStyle(.white)
                                Text(device.room.isEmpty ? "Bez místnosti" : device.room)
                                    .font(.footnote)
                                    .foregroundStyle(Theme.muted)
                            }
                            Spacer()
                            if device.renamed {
                                Image(systemName: "pencil")
                                    .foregroundStyle(Theme.muted)
                            }
                        }
                    }
                }
            } footer: {
                Text("Názvy a místnosti se mění jen pro Jarvise a displej. V aplikaci IKEA Home zůstanou původní.")
            }
        }
        .navigationTitle("Světla a zásuvky")
        .task { await model.loadDevices() }
        .refreshable { await model.loadDevices() }
        .sheet(item: $editing) { device in
            RenameDeviceSheet(device: device)
        }
    }
}

private struct RenameDeviceSheet: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    let device: HomeDevice
    @State private var name = ""
    @State private var room = ""
    @State private var roomless = false

    var body: some View {
        NavigationStack {
            Form {
                Section("Název pro Jarvise") {
                    TextField(device.hubName, text: $name)
                }
                Section {
                    Toggle("Bez místnosti", isOn: $roomless)
                    if !roomless {
                        TextField(device.hubRoom.isEmpty ? "Místnost" : device.hubRoom, text: $room)
                    }
                } header: {
                    Text("Místnost")
                }
                Section {
                    LabeledContent("V IKEA Home", value: device.hubName)
                    LabeledContent("Místnost v IKEA Home", value: device.hubRoom.isEmpty ? "—" : device.hubRoom)
                    if device.renamed {
                        Button("Vrátit původní") {
                            Task {
                                await model.rename(device, name: nil, room: nil)
                                dismiss()
                            }
                        }
                    }
                }
            }
            .navigationTitle(device.name)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Zrušit") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Uložit") {
                        Task {
                            await save()
                            dismiss()
                        }
                    }
                }
            }
        }
        .onAppear {
            name = device.name
            room = device.room
            roomless = device.room.isEmpty
        }
    }

    private func save() async {
        let cleanName = name.trimmingCharacters(in: .whitespaces)
        let cleanRoom = room.trimmingCharacters(in: .whitespaces)
        let newName: String? = cleanName.isEmpty || cleanName == device.hubName ? nil : cleanName
        let newRoom: String? = roomless ? "" : (cleanRoom == device.hubRoom ? nil : cleanRoom)
        await model.rename(device, name: newName, room: newRoom)
    }
}
