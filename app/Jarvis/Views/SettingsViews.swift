import SwiftUI

private struct RestartNote: View {
    var body: some View {
        Text("Po uložení se displej restartuje, trvá to asi 15 sekund.")
    }
}

private struct SaveButton: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    let fields: () -> [String: Any]
    var disabled = false
    @State private var saving = false

    var body: some View {
        Button("Uložit") {
            saving = true
            Task {
                if await model.store(fields()) {
                    dismiss()
                }
                saving = false
            }
        }
        .disabled(saving || disabled)
    }
}

struct VoiceSettings: View {
    let state: DeviceState
    @State private var voice = Voice.charon
    @State private var place = ""

    var body: some View {
        Form {
            Section {
                Picker("Hlas", selection: $voice) {
                    ForEach(Voice.allCases) { Text($0.title).tag($0) }
                }
                TextField("Kde bydlíš (pro počasí)", text: $place)
            } footer: {
                RestartNote()
            }
        }
        .navigationTitle("Hlas a místo")
        .toolbar {
            SaveButton(fields: { ["voice": voice.rawValue, "place": place.trimmingCharacters(in: .whitespaces)] },
                       disabled: place.trimmingCharacters(in: .whitespaces).isEmpty)
        }
        .onAppear {
            voice = Voice(rawValue: state.settings.voice) ?? .charon
            place = state.settings.place
        }
    }
}

struct SearchSettings: View {
    let state: DeviceState
    @State private var key = ""

    var body: some View {
        Form {
            Section {
                LabeledContent("Stav", value: state.settings.searchKey ? "Klíč je uložený" : "Bez klíče")
                SecureField("Nový klíč tvly-…", text: $key)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
            } footer: {
                Text("Jarvis hledá na webu přes Tavily (1 000 dotazů měsíčně zdarma na app.tavily.com). Prázdné pole klíč nemění.")
            }
            if state.settings.searchKey {
                Section {
                    SaveButton(fields: { ["search_key": ""] })
                        .foregroundStyle(.red)
                } footer: {
                    Text("Smaže klíč z displeje.")
                }
            }
        }
        .navigationTitle("Hledání na webu")
        .toolbar {
            SaveButton(fields: { ["search_key": key.trimmingCharacters(in: .whitespaces)] },
                       disabled: !key.trimmingCharacters(in: .whitespaces).hasPrefix("tvly-"))
        }
    }
}

struct TravelSettings: View {
    let state: DeviceState
    @State private var home = ""
    @State private var transport = Transport.scooter

    var body: some View {
        Form {
            Section {
                TextField("Ulice 12, Praha", text: $home)
                Picker("Čím jezdíš", selection: $transport) {
                    ForEach(Transport.allCases) { Text($0.title).tag($0) }
                }
            } footer: {
                Text("Adresa slouží k odhadu, kdy vyrazit na schůzky z kalendáře. Posílá se mapové službě OpenStreetMap.")
            }
        }
        .navigationTitle("Domov a cesta")
        .toolbar {
            SaveButton(fields: { ["home": home.trimmingCharacters(in: .whitespaces), "transport": transport.rawValue] })
        }
        .onAppear {
            home = state.settings.home
            transport = Transport(rawValue: state.settings.transport) ?? .scooter
        }
    }
}

struct WifiSettings: View {
    let state: DeviceState
    @State private var ssid = ""
    @State private var password = ""

    var body: some View {
        Form {
            Section("Teď") {
                LabeledContent("Síť", value: state.network.ssid)
                LabeledContent("Signál", value: "\(state.network.rssi) dBm")
                LabeledContent("Adresa", value: state.network.ip)
            }
            Section {
                TextField("Název sítě", text: $ssid)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                SecureField("Heslo", text: $password)
            } header: {
                Text("Jiná síť")
            } footer: {
                Text("Když se displej k nové síti nepřipojí, nastav ji znovu přes webový flasher. Telefon musí být pak na stejné síti.")
            }
        }
        .navigationTitle("Wi-Fi")
        .toolbar {
            SaveButton(fields: { ["ssid": ssid.trimmingCharacters(in: .whitespaces), "password": password] },
                       disabled: ssid.trimmingCharacters(in: .whitespaces).isEmpty)
        }
    }
}

struct BluetoothView: View {
    var body: some View {
        Form {
            Section {
                Label("Přímo z displeje to nejde", systemImage: "xmark.octagon")
                    .foregroundStyle(.orange)
                Text("Čip v displeji (ESP32-S3) umí jen Bluetooth LE. Reproduktory potřebují klasický Bluetooth, který deska nemá.")
                    .foregroundStyle(Theme.muted)
            }
            Section {
                Label("Přes Raspberry", systemImage: "hifispeaker.and.homepod")
                Text("Raspberry, které už hraje do reproduktoru, bude od Jarvise přebírat zvuk po Wi-Fi. Zapne se tady, až bude Raspberry připravené.")
                    .foregroundStyle(Theme.muted)
            } header: {
                Text("Připravuji")
            }
        }
        .navigationTitle("Bluetooth reproduktor")
    }
}
