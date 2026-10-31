import SwiftUI

struct NoticesView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        List {
            if model.notices.isEmpty {
                Text("Zatím žádné zprávy.")
                    .foregroundStyle(Theme.muted)
            }
            ForEach(model.notices) { notice in
                VStack(alignment: .leading, spacing: 4) {
                    Text(notice.text)
                    Text("\(notice.source) · \(notice.date.formatted(date: .abbreviated, time: .shortened))")
                        .font(.caption)
                        .foregroundStyle(Theme.muted)
                }
            }
        }
        .navigationTitle("Zprávy")
        .task { await model.markNoticesRead() }
    }
}
