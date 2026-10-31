import SwiftUI

struct ArrivalHelpView: View {
    private let steps = [
        "Otevři aplikaci Zkratky, záložku Automatizace, a klepni na plus.",
        "Vyber Wi-Fi, svoji domácí síť a volbu Je připojeno. Zapni Spustit okamžitě.",
        "Přidej akci Přišel jsem domů z aplikace Jarvis.",
        "Stejně vytvoř druhou automatizaci pro Je odpojeno s akcí Odešel jsem z domova.",
    ]

    var body: some View {
        List {
            Section {
                ForEach(Array(steps.enumerated()), id: \.offset) { index, step in
                    HStack(alignment: .top, spacing: 12) {
                        Text("\(index + 1)")
                            .font(.headline)
                            .foregroundStyle(Theme.accent)
                        Text(step)
                    }
                    .padding(.vertical, 2)
                }
            } header: {
                Text("Nastavení ve Zkratkách")
            } footer: {
                Text("Po návratu z delší cesty tě Jarvis přivítá, po setmění rozsvítí vybraná světla a shrne, co je nového. Světla vybereš ve Světla a zásuvky. Když jsi pryč, zprávy chodí do telefonu místo do prázdného bytu.")
            }
        }
        .navigationTitle("Příchod domů")
    }
}
