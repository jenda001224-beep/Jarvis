import SwiftUI

struct PairingView: View {
    @Environment(AppModel.self) private var model
    @State private var discovery = Discovery()
    @State private var manual = "jarvis.local"
    @State private var chosen: String?
    @State private var code = ""
    @State private var working = false
    @State private var message: String?
    @FocusState private var typing: Bool

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 24) {
                    DeviceArt()
                        .frame(maxWidth: 260)
                        .frame(maxWidth: .infinity)
                        .padding(.top, 12)
                    VStack(alignment: .leading, spacing: 6) {
                        Text("J-WALL Display")
                            .font(.largeTitle.weight(.semibold))
                        Text("Spáruj telefon s Jarvisem na zdi. Telefon musí být na stejné Wi-Fi jako displej.")
                            .foregroundStyle(Theme.muted)
                    }
                    if let chosen {
                        codeStep(address: chosen)
                    } else {
                        findStep
                    }
                    if let message {
                        Label(message, systemImage: "exclamationmark.triangle")
                            .font(.footnote)
                            .foregroundStyle(.orange)
                    }
                }
                .padding(20)
            }
            .background(Theme.background.ignoresSafeArea())
        }
        .onAppear { discovery.start() }
        .onDisappear { discovery.stop() }
    }

    private var findStep: some View {
        VStack(alignment: .leading, spacing: 12) {
            Caption(text: "Nalezené displeje")
            if discovery.devices.isEmpty {
                Card {
                    HStack(spacing: 12) {
                        ProgressView()
                        Text("Hledám v síti…")
                            .foregroundStyle(Theme.muted)
                    }
                }
            }
            ForEach(discovery.devices) { device in
                Button {
                    Task { await choose(device) }
                } label: {
                    Card {
                        HStack {
                            Image(systemName: "display")
                                .foregroundStyle(Theme.accent)
                            Text(device.name)
                                .foregroundStyle(.white)
                            Spacer()
                            Image(systemName: "chevron.right")
                                .foregroundStyle(Theme.muted)
                        }
                    }
                }
                .disabled(working)
            }
            Caption(text: "Nebo zadej adresu")
                .padding(.top, 8)
            HStack {
                TextField("jarvis.local nebo 10.0.0.29", text: $manual)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .keyboardType(.URL)
                    .padding(12)
                    .background(Theme.card, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
                Button("Připojit") {
                    Task { await connect(to: manual.trimmingCharacters(in: .whitespaces)) }
                }
                .buttonStyle(.borderedProminent)
                .disabled(working || manual.isEmpty)
            }
        }
    }

    private func codeStep(address: String) -> some View {
        VStack(alignment: .leading, spacing: 14) {
            Caption(text: "Kód z displeje")
            Text("Na displeji se teď ukázal šestimístný kód. Opiš ho sem.")
                .foregroundStyle(Theme.muted)
            TextField("000000", text: $code)
                .keyboardType(.numberPad)
                .textContentType(.oneTimeCode)
                .font(.system(size: 40, weight: .light, design: .rounded))
                .monospacedDigit()
                .multilineTextAlignment(.center)
                .padding(14)
                .background(Theme.card, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
                .focused($typing)
                .onChange(of: code) { _, value in
                    code = String(value.filter(\.isNumber).prefix(6))
                    if code.count == 6 {
                        Task { await finish(address: address) }
                    }
                }
            HStack {
                Button("Zpět") {
                    chosen = nil
                    code = ""
                }
                Spacer()
                Button("Nový kód") {
                    Task { await connect(to: address) }
                }
                .disabled(working)
            }
            .font(.callout)
        }
        .onAppear { typing = true }
    }

    private func choose(_ device: FoundDevice) async {
        working = true
        defer { working = false }
        guard let address = await Discovery.address(of: device.endpoint) else {
            message = "Displej se nepodařilo najít. Zkus zadat adresu."
            return
        }
        await connect(to: address)
    }

    private func connect(to address: String) async {
        working = true
        defer { working = false }
        message = nil
        do {
            _ = try await model.hello(at: address)
            try await model.startPairing(at: address)
            code = ""
            chosen = address
        } catch {
            message = error.localizedDescription
        }
    }

    private func finish(address: String) async {
        working = true
        defer { working = false }
        do {
            try await model.finishPairing(at: address, code: code)
        } catch {
            code = ""
            switch error as? ClientError {
            case .server(401, _): message = "Kód nesedí. Zkus to znovu."
            case .server(429, _): message = "Moc pokusů. Vyžádej si nový kód."
            case .server(409, _): message = "Kód vypršel. Vyžádej si nový."
            default: message = error.localizedDescription
            }
        }
    }
}
