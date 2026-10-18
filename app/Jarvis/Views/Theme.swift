import SwiftUI

enum Theme {
    static let accent = Color(red: 0.36, green: 0.84, blue: 1.0)
    static let background = Color(red: 0.02, green: 0.03, blue: 0.05)
    static let card = Color(red: 0.05, green: 0.08, blue: 0.11)
    static let edge = Color(red: 0.12, green: 0.18, blue: 0.23)
    static let muted = Color(red: 0.55, green: 0.63, blue: 0.70)
}

struct Card<Content: View>: View {
    @ViewBuilder var content: Content

    var body: some View {
        content
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Theme.card, in: RoundedRectangle(cornerRadius: 20, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 20, style: .continuous).stroke(Theme.edge, lineWidth: 1))
    }
}

struct Caption: View {
    let text: String

    var body: some View {
        Text(text.uppercased())
            .font(.caption.weight(.semibold))
            .tracking(2.5)
            .foregroundStyle(Theme.muted)
    }
}

struct OfflineBanner: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        if model.restarting {
            Label("Displej se restartuje…", systemImage: "arrow.clockwise")
                .banner()
        } else if !model.reachable {
            Label("Displej není na dosah. Funguje to jen na stejné Wi-Fi.", systemImage: "wifi.exclamationmark")
                .banner()
        } else if let problem = model.problem {
            Label(problem, systemImage: "exclamationmark.triangle")
                .banner()
                .onTapGesture { model.problem = nil }
        }
    }
}

private extension View {
    func banner() -> some View {
        font(.footnote.weight(.medium))
            .foregroundStyle(.white)
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Color.orange.opacity(0.22), in: RoundedRectangle(cornerRadius: 14, style: .continuous))
            .padding(.horizontal)
    }
}
