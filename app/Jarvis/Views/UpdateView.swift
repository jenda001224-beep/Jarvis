import SwiftUI

struct UpdateView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        Form {
            if let update = model.update {
                Section {
                    LabeledContent("Běží", value: update.current)
                    if update.checked {
                        LabeledContent("Nejnovější", value: update.newest)
                    }
                }
                if update.phase == "downloading" {
                    Section("Aktualizuje se") {
                        ProgressView(value: Double(update.percent), total: 100)
                        Text("Displej se sám restartuje. Když se nová verze nerozběhne, vrátí se k té předchozí.")
                            .font(.footnote)
                            .foregroundStyle(Theme.muted)
                    }
                } else if update.available {
                    Section {
                        Button("Aktualizovat na \(update.newest)") {
                            Task { await model.installUpdate() }
                        }
                    } footer: {
                        Text("Trvá to asi minutu, displej se pak sám restartuje. Nastavení, úkoly i paměť zůstanou.")
                    }
                } else if update.checked {
                    Section {
                        Label("Běží nejnovější verze", systemImage: "checkmark.circle")
                    }
                }
                if !update.problem.isEmpty {
                    Section {
                        Text(update.problem)
                            .foregroundStyle(.orange)
                    }
                }
            } else {
                ProgressView()
            }
            Section {
                Button("Zkontrolovat") {
                    Task { await model.checkForUpdate() }
                }
            }
        }
        .navigationTitle("Aktualizace")
        .task {
            while !Task.isCancelled {
                await model.loadUpdate()
                try? await Task.sleep(nanoseconds: 3_000_000_000)
            }
        }
    }
}
