import SwiftUI

struct ChatView: View {
    @Environment(AppModel.self) private var model
    @State private var text = ""
    @State private var sending = false
    @FocusState private var typing: Bool

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                ScrollViewReader { reader in
                    ScrollView {
                        LazyVStack(spacing: 10) {
                            if model.chat.isEmpty {
                                Text("Napiš Jarvisovi cokoli, odkudkoli. Umí totéž co doma: světla, úkoly, budíky, kalendář i hledání.")
                                    .font(.callout)
                                    .foregroundStyle(Theme.muted)
                                    .multilineTextAlignment(.center)
                                    .padding(.top, 40)
                                    .padding(.horizontal, 30)
                            }
                            ForEach(model.chat) { message in
                                Bubble(message: message)
                                    .id(message.id)
                            }
                            if model.chatBusy {
                                HStack {
                                    ProgressView()
                                        .padding(12)
                                        .background(Theme.card, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                                    Spacer()
                                }
                                .id("thinking")
                            }
                        }
                        .padding(14)
                    }
                    .scrollDismissesKeyboard(.interactively)
                    .onChange(of: model.chat.last?.id) { _, last in
                        if let last {
                            withAnimation { reader.scrollTo(last, anchor: .bottom) }
                        }
                    }
                }
                composer
            }
            .background(Theme.background)
            .navigationTitle("Jarvis")
            .navigationBarTitleDisplayMode(.inline)
            .safeAreaInset(edge: .top) { OfflineBanner() }
            .toolbar {
                if !model.chat.isEmpty {
                    Menu {
                        Button("Smazat konverzaci", role: .destructive) {
                            Task { await model.clearChat() }
                        }
                    } label: {
                        Image(systemName: "ellipsis.circle")
                    }
                }
            }
            .task {
                while !Task.isCancelled {
                    await model.loadChat()
                    try? await Task.sleep(nanoseconds: model.chatBusy ? 1_000_000_000 : 4_000_000_000)
                }
            }
        }
    }

    private var composer: some View {
        HStack(alignment: .bottom, spacing: 10) {
            TextField("Zpráva pro Jarvise", text: $text, axis: .vertical)
                .lineLimit(1...5)
                .focused($typing)
                .padding(.horizontal, 14)
                .padding(.vertical, 10)
                .background(Theme.card, in: RoundedRectangle(cornerRadius: 20, style: .continuous))
                .overlay(RoundedRectangle(cornerRadius: 20, style: .continuous).stroke(Theme.edge, lineWidth: 1))
            Button {
                let message = text
                text = ""
                sending = true
                Task {
                    if !(await model.sendChat(message)) {
                        text = message
                    }
                    sending = false
                }
            } label: {
                Image(systemName: "arrow.up.circle.fill")
                    .font(.system(size: 32))
            }
            .disabled(sending || text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
    }
}

private struct Bubble: View {
    let message: ChatMessage

    var body: some View {
        HStack {
            if message.mine {
                Spacer(minLength: 50)
            }
            Text(message.text)
                .textSelection(.enabled)
                .padding(.horizontal, 14)
                .padding(.vertical, 10)
                .foregroundStyle(message.mine ? Color.black : .white)
                .background(message.mine ? Theme.accent : Theme.card, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
            if !message.mine {
                Spacer(minLength: 50)
            }
        }
    }
}
