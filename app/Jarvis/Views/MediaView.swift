import SwiftUI

struct MediaView: View {
    @Environment(AppModel.self) private var model

    private let noises = [("Bílý", "white"), ("Růžový", "pink"), ("Hnědý", "brown")]

    var body: some View {
        List {
            if let media = model.media, media.playing {
                Section("Hraje") {
                    HStack {
                        Text(media.title)
                            .foregroundStyle(Theme.accent)
                        Spacer()
                        Button {
                            Task { await model.stopMedia() }
                        } label: {
                            Image(systemName: "stop.circle.fill")
                                .font(.title2)
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            Section("Rádio") {
                ForEach(model.media?.stations ?? [], id: \.self) { station in
                    Button(station) {
                        Task { await model.play(station: station) }
                    }
                    .foregroundStyle(.white)
                }
            }
            Section {
                ForEach(noises, id: \.1) { noise in
                    Button(noise.0) {
                        Task { await model.play(noise: noise.1) }
                    }
                    .foregroundStyle(.white)
                }
            } header: {
                Text("Šum na usínání")
            } footer: {
                Text("Hraje hodinu a pak se ztiší. Hlasem si řekni i o jinou stanici nebo podcast. Zvuk je mono a jde z malého reproduktoru displeje.")
            }
        }
        .navigationTitle("Přehrávání")
    }
}
