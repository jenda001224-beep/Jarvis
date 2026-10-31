import SwiftUI

struct MemoryView: View {
    @Environment(AppModel.self) private var model
    @State private var notes = ""
    @State private var edited = false
    @State private var confirmWipe = false

    var body: some View {
        Form {
            Section {
                Toggle("Pamatovat si rozhovory", isOn: Binding(get: { model.memory?.enabled ?? true }, set: { on in
                    Task { await model.setMemory(enabled: on) }
                }))
            } footer: {
                Text("Jarvis si ukládá všechno, co mu řekneš, a sám si z toho píše poznámky. Rozhovory zůstávají na displeji, poznámky jdou s každým dotazem do Gemini.")
            }
            Section("Co o tobě ví") {
                TextEditor(text: $notes)
                    .frame(minHeight: 200)
                    .onChange(of: notes) { _, fresh in
                        edited = fresh != (model.memory?.notes ?? "")
                    }
                if edited {
                    Button("Uložit změny") {
                        Task {
                            await model.saveNotes(notes)
                            edited = false
                        }
                    }
                }
            }
            Section {
                NavigationLink {
                    MemoryHistoryView()
                } label: {
                    LabeledContent("Historie rozhovorů", value: "\(model.memory?.lines ?? 0) vět")
                }
            }
            Section {
                Button("Smazat celou paměť", role: .destructive) {
                    confirmWipe = true
                }
            }
        }
        .navigationTitle("Paměť")
        .confirmationDialog("Smazat všechny poznámky a rozhovory?", isPresented: $confirmWipe, titleVisibility: .visible) {
            Button("Smazat", role: .destructive) {
                Task {
                    await model.wipeMemory()
                    notes = ""
                    edited = false
                }
            }
        }
        .task {
            await model.loadMemory()
            notes = model.memory?.notes ?? ""
            edited = false
        }
    }
}

private struct MemoryHistoryView: View {
    @Environment(AppModel.self) private var model
    @State private var query = ""

    var body: some View {
        List(model.memoryLines) { line in
            VStack(alignment: .leading, spacing: 4) {
                Text("\(line.who) · \(line.when)")
                    .font(.caption)
                    .foregroundStyle(Theme.muted)
                Text(line.text)
            }
        }
        .navigationTitle("Historie")
        .searchable(text: $query, prompt: "Hledat v rozhovorech")
        .onSubmit(of: .search) {
            Task { await model.searchMemory(query) }
        }
        .onChange(of: query) { _, fresh in
            if fresh.isEmpty {
                Task { await model.searchMemory("") }
            }
        }
        .task { await model.searchMemory("") }
    }
}
