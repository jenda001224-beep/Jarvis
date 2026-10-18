import SwiftUI

struct DeviceView: View {
    @Environment(AppModel.self) private var model
    @State private var prefs: Prefs?
    @State private var sliding = false
    @State private var saying = false
    @State private var confirmRestart = false
    @State private var confirmForget = false

    var body: some View {
        NavigationStack {
            List {
                Section {
                    header
                }
                .listRowBackground(Color.clear)
                .listRowInsets(EdgeInsets())
                if let state = model.state, prefs != nil {
                    actions(state)
                    sound
                    display
                    night
                    jarvis(state)
                    network(state)
                    about(state)
                }
                Section {
                    Button("Odpárovat tento telefon", role: .destructive) {
                        confirmForget = true
                    }
                }
            }
            .scrollContentBackground(.hidden)
            .background(Theme.background)
            .navigationTitle("Zařízení")
            .navigationBarTitleDisplayMode(.inline)
            .safeAreaInset(edge: .top) { OfflineBanner() }
            .refreshable { await model.refresh() }
            .sheet(isPresented: $saying) {
                SaySheet()
                    .presentationDetents([.medium])
            }
            .confirmationDialog("Restartovat displej?", isPresented: $confirmRestart, titleVisibility: .visible) {
                Button("Restartovat") { Task { await model.restart() } }
            }
            .confirmationDialog("Odpárovat telefon?", isPresented: $confirmForget, titleVisibility: .visible) {
                Button("Odpárovat", role: .destructive) { Task { await model.forget() } }
            } message: {
                Text("Pro další spojení bude potřeba nový kód z displeje.")
            }
            .onAppear { prefs = model.state?.prefs }
            .onChange(of: model.state?.prefs) { _, fresh in
                if !sliding, let fresh {
                    prefs = fresh
                }
            }
        }
    }

    private var header: some View {
        VStack(spacing: 14) {
            DeviceArt()
                .frame(maxWidth: 240)
                .padding(.top, 8)
            VStack(spacing: 4) {
                Text(model.state?.name ?? "J-WALL Display")
                    .font(.title.weight(.semibold))
                Text("Waveshare ESP32-S3 Touch LCD 4B")
                    .font(.footnote)
                    .foregroundStyle(Theme.muted)
                status
                    .padding(.top, 4)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.bottom, 8)
    }

    private var status: some View {
        let online = model.reachable && model.state != nil
        let text: String
        switch model.state?.state {
        case _ where !online: text = "Nedostupný"
        case "listening": text = "Poslouchá"
        case "speaking": text = "Mluví"
        case "connecting": text = "Připojuje se"
        case "ringing": text = "Zvoní budík"
        case "offline": text = "Bez internetu"
        case "error": text = "Chyba"
        default: text = "Připraven"
        }
        return Label(text, systemImage: "circle.fill")
            .font(.caption.weight(.semibold))
            .foregroundStyle(online ? Theme.accent : .orange)
            .labelStyle(StatusLabelStyle())
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(Theme.card, in: Capsule())
    }

    private func actions(_ state: DeviceState) -> some View {
        Section {
            HStack(spacing: 10) {
                ActionTile(title: state.lamp ? "Zhasnout" : "Lampička", symbol: state.lamp ? "lightbulb.fill" : "lightbulb",
                           active: state.lamp) {
                    Task { await model.setLamp(!state.lamp) }
                }
                ActionTile(title: "Vzkaz", symbol: "text.bubble", active: false) {
                    saying = true
                }
                ActionTile(title: "Restart", symbol: "arrow.clockwise", active: false) {
                    confirmRestart = true
                }
            }
            .listRowBackground(Color.clear)
            .listRowInsets(EdgeInsets())
        }
    }

    private var sound: some View {
        Section("Zvuk") {
            LevelRow(title: "Hlasitost", symbol: "speaker.wave.2.fill", range: 0...100, value: prefsBinding(\.volume),
                     sliding: $sliding, commit: commit)
            NavigationLink {
                BluetoothView()
            } label: {
                LabeledContent {
                    Text("Přes Raspberry")
                        .foregroundStyle(Theme.muted)
                } label: {
                    Label("Bluetooth reproduktor", systemImage: "hifispeaker")
                }
            }
        }
    }

    private var display: some View {
        Section("Displej") {
            LevelRow(title: "Jas", symbol: "sun.max.fill", range: 10...100, value: prefsBinding(\.brightness),
                     sliding: $sliding, commit: commit)
            Picker(selection: prefsBinding(\.sleep, commitNow: true)) {
                ForEach(SleepChoice.allCases) { Text($0.title).tag($0.rawValue) }
            } label: {
                Label("Uspat po", systemImage: "moon.zzz")
            }
            Picker(selection: prefsBinding(\.dark, commitNow: true)) {
                Text("Ztlumené hodiny").tag(0)
                Text("Tma").tag(1)
            } label: {
                Label("Po uspání", systemImage: "clock")
            }
            Picker(selection: prefsBinding(\.orientation, commitNow: true)) {
                Text("0°").tag(0)
                Text("90°").tag(1)
                Text("180°").tag(2)
                Text("270°").tag(3)
            } label: {
                Label("Orientace", systemImage: "rotate.right")
            }
        }
    }

    private var night: some View {
        Section {
            Toggle(isOn: prefsBinding(\.night, commitNow: true)) {
                Label("Noční režim", systemImage: "moon.stars")
            }
            if prefs?.night == true {
                DatePicker("Od", selection: minutesBinding(\.nightFrom), displayedComponents: .hourAndMinute)
                DatePicker("Do", selection: minutesBinding(\.nightTo), displayedComponents: .hourAndMinute)
            }
        } header: {
            Text("Noc")
        } footer: {
            Text("V noci displej po půl minutě klidu úplně zhasne, místo hodin je tma. Probudí ho dotyk nebo oslovení.")
        }
    }

    private func jarvis(_ state: DeviceState) -> some View {
        Section("Jarvis") {
            Picker(selection: prefsBinding(\.sensitivity, commitNow: true)) {
                Text("Nízká").tag(0)
                Text("Střední").tag(1)
                Text("Vysoká").tag(2)
            } label: {
                Label("Citlivost na oslovení", systemImage: "ear")
            }
            NavigationLink {
                VoiceSettings(state: state)
            } label: {
                LabeledContent {
                    Text(state.settings.voice).foregroundStyle(Theme.muted)
                } label: {
                    Label("Hlas a místo", systemImage: "waveform")
                }
            }
            NavigationLink {
                SearchSettings(state: state)
            } label: {
                LabeledContent {
                    Text(state.settings.searchKey ? "Zapnuto" : "Vypnuto").foregroundStyle(Theme.muted)
                } label: {
                    Label("Hledání na webu", systemImage: "magnifyingglass")
                }
            }
            NavigationLink {
                TravelSettings(state: state)
            } label: {
                LabeledContent {
                    Text(state.settings.home.isEmpty ? "Nenastaveno" : state.settings.home)
                        .lineLimit(1)
                        .foregroundStyle(Theme.muted)
                } label: {
                    Label("Domov a cesta", systemImage: "house")
                }
            }
        }
    }

    private func network(_ state: DeviceState) -> some View {
        Section("Síť") {
            NavigationLink {
                WifiSettings(state: state)
            } label: {
                LabeledContent {
                    Text("\(state.network.ssid) · \(state.network.rssi) dBm").foregroundStyle(Theme.muted)
                } label: {
                    Label("Wi-Fi", systemImage: "wifi")
                }
            }
        }
    }

    private func about(_ state: DeviceState) -> some View {
        Section("Informace") {
            LabeledContent("Verze", value: state.version)
            LabeledContent("IP adresa", value: state.network.ip)
            LabeledContent("Běží", value: Duration.seconds(state.uptime).formatted(.units(allowed: [.days, .hours, .minutes],
                                                                                              width: .abbreviated)))
            LabeledContent("Volná paměť", value: "\(state.memory.internal / 1024) kB + \(state.memory.external / 1024) kB")
            LabeledContent("IKEA hub", value: hubText(state.hub))
            LabeledContent("Tiskárna", value: state.settings.printer ? "Nastavená" : "Nenastavená")
            LabeledContent("Kalendáře", value: "\(state.settings.calendars)")
        }
    }

    private func hubText(_ hub: Hub) -> String {
        switch hub.state {
        case "ready": return "\(hub.devices) zařízení"
        case "unpaired": return "Nespárovaný"
        case "pairing": return "Páruje se"
        default: return "Hledá se"
        }
    }

    private func commit() {
        guard let prefs else {
            return
        }
        Task { await model.apply(prefs) }
    }

    private func prefsBinding<Value>(_ path: WritableKeyPath<Prefs, Value>, commitNow: Bool = false) -> Binding<Value> {
        Binding(get: { prefs![keyPath: path] }, set: { value in
            prefs?[keyPath: path] = value
            if commitNow {
                commit()
            }
        })
    }

    private func minutesBinding(_ path: WritableKeyPath<Prefs, Int>) -> Binding<Date> {
        Binding(get: {
            let minutes = prefs?[keyPath: path] ?? 0
            return Calendar.current.date(bySettingHour: minutes / 60, minute: minutes % 60, second: 0, of: Date()) ?? Date()
        }, set: { date in
            let parts = Calendar.current.dateComponents([.hour, .minute], from: date)
            prefs?[keyPath: path] = (parts.hour ?? 0) * 60 + (parts.minute ?? 0)
            commit()
        })
    }
}

private struct StatusLabelStyle: LabelStyle {
    func makeBody(configuration: Configuration) -> some View {
        HStack(spacing: 6) {
            configuration.icon.font(.system(size: 7))
            configuration.title
        }
    }
}

private struct ActionTile: View {
    let title: String
    let symbol: String
    let active: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 8) {
                Image(systemName: symbol)
                    .font(.title2)
                Text(title)
                    .font(.footnote.weight(.medium))
            }
            .frame(maxWidth: .infinity, minHeight: 78)
            .foregroundStyle(active ? Color.black : .white)
            .background(active ? Theme.accent : Theme.card, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 18, style: .continuous).stroke(Theme.edge, lineWidth: active ? 0 : 1))
        }
        .buttonStyle(.plain)
    }
}

private struct LevelRow: View {
    let title: String
    let symbol: String
    let range: ClosedRange<Int>
    @Binding var value: Int
    @Binding var sliding: Bool
    let commit: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Label(title, systemImage: symbol)
                Spacer()
                Text("\(value) %")
                    .monospacedDigit()
                    .foregroundStyle(Theme.muted)
            }
            Slider(value: Binding(get: { Double(value) }, set: { value = Int($0.rounded()) }),
                   in: Double(range.lowerBound)...Double(range.upperBound), step: 5) { editing in
                sliding = editing
                if !editing {
                    commit()
                }
            }
        }
        .padding(.vertical, 4)
    }
}

private struct SaySheet: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var text = ""
    @State private var sending = false

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("Třeba: Přijdu za hodinu, nečekejte s večeří.", text: $text, axis: .vertical)
                        .lineLimit(3...6)
                } footer: {
                    Text("Jarvis vzkaz doma přečte nahlas.")
                }
            }
            .navigationTitle("Vzkaz domů")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Zrušit") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Přečíst") {
                        sending = true
                        Task {
                            if await model.say(text) {
                                dismiss()
                            }
                            sending = false
                        }
                    }
                    .disabled(sending || text.trimmingCharacters(in: .whitespaces).isEmpty || text.count > 160)
                }
            }
        }
    }
}
