import AppIntents
import Foundation

enum HomeEvents {
    private static let leftKey = "leftHomeAt"
    private static let addressKey = "deviceAddress"
    private static let shortestAbsence: TimeInterval = 15 * 60

    static func arrived() async {
        guard let address = UserDefaults.standard.string(forKey: addressKey), let token = Keychain.read(),
              let base = URL(string: "http://\(address)/") else {
            return
        }
        let left = UserDefaults.standard.object(forKey: leftKey) as? Date
        UserDefaults.standard.removeObject(forKey: leftKey)
        let client = JarvisClient(base: base, token: token)
        let greet = left.map { Date().timeIntervalSince($0) >= shortestAbsence } ?? true
        for _ in 0..<5 {
            do {
                if greet {
                    _ = try await client.send("POST", "api/arrived", fields: [:], as: Empty.self)
                } else {
                    _ = try await client.send("POST", "api/presence", fields: ["home": true], as: Empty.self)
                }
                return
            } catch {
                try? await Task.sleep(nanoseconds: 4_000_000_000)
            }
        }
    }

    static func left() async {
        UserDefaults.standard.set(Date(), forKey: leftKey)
        guard let token = Keychain.read() else {
            return
        }
        let body = try? JSONSerialization.data(withJSONObject: ["home": false])
        _ = try? await CloudMail(token: token).send(method: "POST", path: "/api/presence", body: body)
    }
}

struct ArrivedHomeIntent: AppIntent {
    static var title: LocalizedStringResource = "Přišel jsem domů"
    static var description = IntentDescription("Jarvis tě přivítá, po setmění rozsvítí a shrne, co je nového.")
    static var openAppWhenRun = false

    func perform() async throws -> some IntentResult {
        await HomeEvents.arrived()
        return .result()
    }
}

struct LeftHomeIntent: AppIntent {
    static var title: LocalizedStringResource = "Odešel jsem z domova"
    static var description = IntentDescription("Jarvis přestane mluvit do prázdného bytu a zprávy pošle do telefonu.")
    static var openAppWhenRun = false

    func perform() async throws -> some IntentResult {
        await HomeEvents.left()
        return .result()
    }
}

struct JarvisShortcuts: AppShortcutsProvider {
    static var appShortcuts: [AppShortcut] {
        AppShortcut(intent: ArrivedHomeIntent(), phrases: ["Přišel jsem domů v \(.applicationName)"], shortTitle: "Přišel jsem domů",
                    systemImageName: "house")
        AppShortcut(intent: LeftHomeIntent(), phrases: ["Odešel jsem v \(.applicationName)"], shortTitle: "Odešel jsem",
                    systemImageName: "figure.walk.departure")
    }
}
