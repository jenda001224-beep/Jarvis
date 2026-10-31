import SwiftUI

struct AppleCalendarView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    let state: DeviceState
    @State private var appleId = ""
    @State private var password = ""
    @State private var chosen = ""
    @State private var saving = false

    private var connected: Bool { model.calendars?.configured == true }

    var body: some View {
        Form {
            if connected {
                Section {
                    LabeledContent("Účet", value: model.calendars?.appleId ?? "")
                    if let error = model.calendars?.error, !error.isEmpty {
                        Text(error)
                            .foregroundStyle(.orange)
                    }
                    if let names = model.calendars?.calendars, !names.isEmpty {
                        Picker("Zapisovat do", selection: $chosen) {
                            Text("První v seznamu").tag("")
                            ForEach(names, id: \.self) { Text($0).tag($0) }
                        }
                        .onChange(of: chosen) { _, value in
                            if value != (model.calendars?.chosen ?? "") {
                                save(["apple_calendar": value])
                            }
                        }
                    }
                } footer: {
                    Text("Řekni třeba „Jarvisi, dej mi v pátek v deset doktora“.")
                }
                Section {
                    Button("Odpojit účet", role: .destructive) {
                        save(["apple_id": "", "apple_password": "", "apple_calendar": ""])
                    }
                }
            } else {
                Section {
                    TextField("Apple ID", text: $appleId)
                        .textInputAutocapitalization(.never)
                        .keyboardType(.emailAddress)
                        .autocorrectionDisabled()
                    SecureField("Heslo pro aplikaci", text: $password)
                } footer: {
                    Text("Běžné heslo nefunguje. Na account.apple.com v části Zabezpečení vytvoř heslo pro aplikaci a vlož ho sem. Uloží se jen na displeji. Po uložení se displej restartuje.")
                }
                Section {
                    Link("Otevřít account.apple.com", destination: URL(string: "https://account.apple.com")!)
                }
            }
        }
        .navigationTitle("Apple kalendář")
        .toolbar {
            if !connected {
                Button("Uložit") {
                    save(["apple_id": appleId.trimmingCharacters(in: .whitespaces), "apple_password": password.replacingOccurrences(of: " ", with: "")])
                }
                .disabled(saving || appleId.isEmpty || password.isEmpty)
            }
        }
        .task {
            await model.loadCalendars()
            chosen = model.calendars?.chosen ?? ""
        }
    }

    private func save(_ fields: [String: Any]) {
        saving = true
        Task {
            if await model.store(fields) {
                dismiss()
            }
            saving = false
        }
    }
}
