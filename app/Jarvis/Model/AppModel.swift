import Foundation
import Observation

@MainActor
@Observable
final class AppModel {
    private static let addressKey = "deviceAddress"
    private static let refreshSeconds: UInt64 = 3

    private(set) var address: String?
    private(set) var token: String?
    private(set) var state: DeviceState?
    private(set) var todos: [Todo] = []
    private(set) var alarms: [Alarm] = []
    private(set) var reminders: [Reminder] = []
    private(set) var reachable = true
    var problem: String?
    var restarting = false

    private var seenRevision = -1
    private var polling: Task<Void, Never>?

    init() {
        address = UserDefaults.standard.string(forKey: AppModel.addressKey)
        token = Keychain.read()
    }

    var paired: Bool { address != nil && token != nil }

    private func client(at address: String? = nil, authorized: Bool = true) -> JarvisClient? {
        guard let address = address ?? self.address, let base = URL(string: "http://\(address)/") else {
            return nil
        }
        return JarvisClient(base: base, token: authorized ? token : nil)
    }

    // MARK: Pairing

    func hello(at address: String) async throws -> Hello {
        guard let client = client(at: address, authorized: false) else {
            throw ClientError.unreachable
        }
        return try await client.send("GET", "api/hello", as: Hello.self)
    }

    func startPairing(at address: String) async throws {
        guard let client = client(at: address, authorized: false) else {
            throw ClientError.unreachable
        }
        _ = try await client.send("POST", "api/pair/start", fields: [:], as: Empty.self)
    }

    func finishPairing(at address: String, code: String) async throws {
        guard let client = client(at: address, authorized: false) else {
            throw ClientError.unreachable
        }
        let reply = try await client.send("POST", "api/pair", fields: ["code": code], as: PairReply.self)
        Keychain.write(reply.token)
        UserDefaults.standard.set(address, forKey: AppModel.addressKey)
        self.address = address
        self.token = reply.token
        seenRevision = -1
        await refresh()
    }

    func forget() async {
        if let client = client() {
            _ = try? await client.send("POST", "api/forget", fields: [:], as: Empty.self)
        }
        unpair()
    }

    private func unpair() {
        Keychain.write(nil)
        UserDefaults.standard.removeObject(forKey: AppModel.addressKey)
        token = nil
        address = nil
        state = nil
        todos = []
        alarms = []
        reminders = []
        stopPolling()
    }

    // MARK: Refresh

    func startPolling() {
        guard polling == nil else {
            return
        }
        polling = Task { [weak self] in
            while !Task.isCancelled {
                await self?.refresh()
                try? await Task.sleep(nanoseconds: AppModel.refreshSeconds * 1_000_000_000)
            }
        }
    }

    func stopPolling() {
        polling?.cancel()
        polling = nil
    }

    func refresh() async {
        guard let client = client() else {
            return
        }
        do {
            let fresh = try await client.send("GET", "api/state", as: DeviceState.self)
            state = fresh
            reachable = true
            restarting = false
            if fresh.revision != seenRevision {
                async let todoList = client.send("GET", "api/todos", as: TodoList.self)
                async let alarmList = client.send("GET", "api/alarms", as: AlarmList.self)
                async let reminderList = client.send("GET", "api/reminders", as: ReminderList.self)
                todos = try await todoList.todos
                alarms = try await alarmList.alarms
                reminders = try await reminderList.reminders
                seenRevision = fresh.revision
            }
        } catch ClientError.unauthorized {
            unpair()
        } catch {
            reachable = false
        }
    }

    private func perform<Reply: Decodable>(_ work: (JarvisClient) async throws -> Reply) async -> Reply? {
        guard let client = client() else {
            return nil
        }
        do {
            let reply = try await work(client)
            problem = nil
            return reply
        } catch ClientError.unauthorized {
            unpair()
        } catch {
            problem = error.localizedDescription
        }
        return nil
    }

    // MARK: To-dos

    func addTodo(_ text: String) async {
        let clean = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !clean.isEmpty else {
            return
        }
        if let list = await perform({ try await $0.send("POST", "api/todos", fields: ["text": clean], as: TodoList.self) }) {
            todos = list.todos
        }
    }

    func setDone(_ todo: Todo, _ done: Bool) async {
        if let index = todos.firstIndex(of: todo) {
            todos[index].done = done
        }
        if let list = await perform({ try await $0.send("PATCH", "api/todos/\(todo.id)", fields: ["done": done], as: TodoList.self) }) {
            todos = list.todos
        }
    }

    func rename(_ todo: Todo, to text: String) async {
        let clean = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !clean.isEmpty, clean != todo.text else {
            return
        }
        if let list = await perform({ try await $0.send("PATCH", "api/todos/\(todo.id)", fields: ["text": clean], as: TodoList.self) }) {
            todos = list.todos
        }
    }

    func delete(_ todo: Todo) async {
        todos.removeAll { $0.id == todo.id }
        if let list = await perform({ try await $0.send("DELETE", "api/todos/\(todo.id)", as: TodoList.self) }) {
            todos = list.todos
        }
    }

    func clearDone() async {
        if let list = await perform({ try await $0.send("POST", "api/todos/clear", fields: [:], as: TodoList.self) }) {
            todos = list.todos
        }
    }

    // MARK: Alarms and reminders

    func save(_ draft: AlarmDraft, id: Int?) async {
        let reply: AlarmList?
        if let id {
            reply = await perform { try await $0.send("PATCH", "api/alarms/\(id)", body: draft, as: AlarmList.self) }
        } else {
            reply = await perform { try await $0.send("POST", "api/alarms", body: draft, as: AlarmList.self) }
        }
        if let reply {
            alarms = reply.alarms
            await refresh()
        }
    }

    func setEnabled(_ alarm: Alarm, _ enabled: Bool) async {
        let draft = AlarmDraft(hour: alarm.hour, minute: alarm.minute, days: alarm.days, label: alarm.label, enabled: enabled)
        await save(draft, id: alarm.id)
    }

    func delete(_ alarm: Alarm) async {
        alarms.removeAll { $0.id == alarm.id }
        if let list = await perform({ try await $0.send("DELETE", "api/alarms/\(alarm.id)", as: AlarmList.self) }) {
            alarms = list.alarms
        }
    }

    func delete(_ reminder: Reminder) async {
        reminders.removeAll { $0.id == reminder.id }
        if let list = await perform({ try await $0.send("DELETE", "api/reminders/\(reminder.id)", as: ReminderList.self) }) {
            reminders = list.reminders
        }
    }

    func stopAlarm() async {
        _ = await perform { try await $0.send("POST", "api/alarm/stop", fields: [:], as: DeviceState.self) }
        await refresh()
    }

    // MARK: Device

    func apply(_ prefs: Prefs) async {
        _ = await perform { try await $0.send("PUT", "api/prefs", body: prefs, as: PrefsReply.self) }
        await refresh()
    }

    func store(_ fields: [String: Any]) async -> Bool {
        let reply = await perform { try await $0.send("PUT", "api/settings", fields: fields, as: Empty.self) }
        if reply != nil {
            restarting = true
        }
        return reply != nil
    }

    func setLamp(_ on: Bool) async {
        _ = await perform { try await $0.send("POST", "api/lamp", fields: ["on": on], as: DeviceState.self) }
        await refresh()
    }

    func say(_ text: String) async -> Bool {
        let clean = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !clean.isEmpty else {
            return false
        }
        return await perform { try await $0.send("POST", "api/say", fields: ["text": clean], as: Empty.self) } != nil
    }

    func restart() async {
        if await perform({ try await $0.send("POST", "api/restart", fields: [:], as: Empty.self) }) != nil {
            restarting = true
        }
    }
}
