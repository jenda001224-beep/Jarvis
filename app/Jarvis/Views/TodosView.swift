import SwiftUI

struct TodosView: View {
    @Environment(AppModel.self) private var model
    @State private var draft = ""
    @State private var editing: Todo?
    @FocusState private var adding: Bool

    private var open: [Todo] { model.todos.filter { !$0.done } }
    private var finished: [Todo] { model.todos.filter(\.done) }

    var body: some View {
        NavigationStack {
            List {
                Section {
                    HStack(spacing: 12) {
                        Image(systemName: "plus.circle.fill")
                            .font(.title3)
                            .foregroundStyle(Theme.accent)
                        TextField("Nový úkol", text: $draft)
                            .focused($adding)
                            .submitLabel(.done)
                            .onSubmit(add)
                    }
                }
                if open.isEmpty && finished.isEmpty {
                    Section {
                        Text("Seznam je prázdný. Řekni „Jarvisi, zapiš si…“ nebo úkol přidej tady.")
                            .font(.callout)
                            .foregroundStyle(Theme.muted)
                    }
                }
                if !open.isEmpty {
                    Section("Zbývá \(open.count)") {
                        ForEach(open) { row($0) }
                    }
                }
                if !finished.isEmpty {
                    Section("Hotovo") {
                        ForEach(finished) { row($0) }
                    }
                }
            }
            .scrollContentBackground(.hidden)
            .background(Theme.background)
            .navigationTitle("Úkoly")
            .toolbar {
                if !finished.isEmpty {
                    Button("Smazat hotové") {
                        Task { await model.clearDone() }
                    }
                }
            }
            .safeAreaInset(edge: .top) { OfflineBanner() }
            .refreshable { await model.refresh() }
            .sheet(item: $editing) { todo in
                RenameSheet(todo: todo)
                    .presentationDetents([.height(220)])
            }
        }
    }

    private func row(_ todo: Todo) -> some View {
        HStack(spacing: 12) {
            Button {
                Task { await model.setDone(todo, !todo.done) }
            } label: {
                Image(systemName: todo.done ? "checkmark.circle.fill" : "circle")
                    .font(.title3)
                    .foregroundStyle(todo.done ? Theme.accent : Theme.muted)
            }
            .buttonStyle(.plain)
            Text(todo.text)
                .strikethrough(todo.done)
                .foregroundStyle(todo.done ? Theme.muted : .white)
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
                .onTapGesture { editing = todo }
        }
        .swipeActions {
            Button(role: .destructive) {
                Task { await model.delete(todo) }
            } label: {
                Label("Smazat", systemImage: "trash")
            }
        }
    }

    private func add() {
        let text = draft
        draft = ""
        Task { await model.addTodo(text) }
        adding = true
    }
}

private struct RenameSheet: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    let todo: Todo
    @State private var text = ""

    var body: some View {
        NavigationStack {
            Form {
                TextField("Úkol", text: $text, axis: .vertical)
            }
            .navigationTitle("Upravit úkol")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Zrušit") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Uložit") {
                        Task {
                            await model.rename(todo, to: text)
                            dismiss()
                        }
                    }
                }
            }
        }
        .onAppear { text = todo.text }
    }
}
