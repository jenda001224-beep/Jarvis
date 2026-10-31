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
    private(set) var devices: [HomeDevice] = []
    private(set) var reachable = true
    private(set) var remote = false
    private(set) var timers: [TimerItem] = []
    private(set) var timersReceived = Date()
    private(set) var notices: [Notice] = []
    private(set) var unread = 0
    private(set) var memory: MemoryStatus?
    private(set) var memoryLines: [MemoryLine] = []
    private(set) var media: MediaStatus?
    private(set) var chat: [ChatMessage] = []
    private(set) var chatBusy = false
    private(set) var calendars: CalendarStatus?
    var problem: String?
    var restarting = false

    private var seenRevision = -1
    private var lastLocalTry = Date.distantPast
    private var mail: CloudMail?
    private static let localRetrySeconds: TimeInterval = 30
    private static let remoteRefreshSeconds: UInt64 = 8
    private var polling: Task<Void, Never>?

    init() {
        address = UserDefaults.standard.string(forKey: AppModel.addressKey)
        token = Keychain.read()
        if let token {
            mail = CloudMail(token: token)
        }
    }

    var paired: Bool { address != nil && token != nil }

    private func client(at address: String? = nil, authorized: Bool = true) -> JarvisClient? {
        guard let address = address ?? self.address, let base = URL(string: "http://\(address)/") else {
            return nil
        }
        return JarvisClient(base: base, token: authorized ? token : nil, mail: remote && authorized ? mail : nil)
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
        mail = CloudMail(token: reply.token)
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
        mail = nil
        remote = false
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
                let pause = self?.remote == true ? AppModel.remoteRefreshSeconds : AppModel.refreshSeconds
                try? await Task.sleep(nanoseconds: pause * 1_000_000_000)
            }
        }
    }

    func stopPolling() {
        polling?.cancel()
        polling = nil
    }

    func refresh() async {
        guard let address, let base = URL(string: "http://\(address)/") else {
            return
        }
        if remote && Date().timeIntervalSince(lastLocalTry) < AppModel.localRetrySeconds {
            await refreshRemote()
            return
        }
        lastLocalTry = Date()
        let client = JarvisClient(base: base, token: token)
        do {
            let fresh = try await client.send("GET", "api/state", as: DeviceState.self)
            state = fresh
            reachable = true
            remote = false
            restarting = false
            await refreshExtras(client)
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
            await refreshRemote()
        }
    }

    private func refreshExtras(_ client: JarvisClient) async {
        if let list = try? await client.send("GET", "api/timers", as: TimerList.self) {
            timers = list.timers
            timersReceived = Date()
        }
        if let list = try? await client.send("GET", "api/notices", as: NoticeList.self) {
            notices = list.notices
            unread = list.unread
        }
        media = try? await client.send("GET", "api/media", as: MediaStatus.self)
    }

    private func refreshRemote() async {
        guard let mail, let data = try? await mail.snapshot(),
              let snapshot = try? JarvisClient.decoder.decode(Snapshot.self, from: data) else {
            remote = false
            reachable = false
            return
        }
        remote = true
        reachable = true
        restarting = false
        state = snapshot.state
        todos = snapshot.todos ?? todos
        alarms = snapshot.alarms ?? alarms
        if let list = snapshot.timers {
            timers = list
            timersReceived = Date()
        }
        media = snapshot.media ?? media
        if let log = snapshot.chat {
            chat = log.messages
            chatBusy = log.busy
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

    // MARK: Lights and outlets

    func loadDevices() async {
        if let list = await perform({ try await $0.send("GET", "api/devices", as: HomeDeviceList.self) }) {
            devices = list.devices
        }
    }

    func rename(_ device: HomeDevice, name: String?, room: String?) async {
        let fields: [String: Any] = ["name": name ?? NSNull(), "room": room ?? NSNull()]
        if let list = await perform({ try await $0.send("PUT", "api/devices/\(device.id)", fields: fields, as: HomeDeviceList.self) }) {
            devices = list.devices
        }
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

    // MARK: Timers

    func addTimer(minutes: Int, label: String = "") async {
        if let list = await perform({ try await $0.send("POST", "api/timers", fields: ["seconds": minutes * 60, "label": label], as: TimerList.self) }) {
            timers = list.timers
            timersReceived = Date()
        }
    }

    func cancel(_ timer: TimerItem) async {
        timers.removeAll { $0.id == timer.id }
        if let list = await perform({ try await $0.send("DELETE", "api/timers/\(timer.id)", as: TimerList.self) }) {
            timers = list.timers
            timersReceived = Date()
        }
    }

    // MARK: Notices

    func markNoticesRead() async {
        if let list = await perform({ try await $0.send("DELETE", "api/notices", as: NoticeList.self) }) {
            notices = list.notices
            unread = list.unread
        }
    }

    // MARK: Memory

    func loadMemory() async {
        memory = await perform { try await $0.send("GET", "api/memory", as: MemoryStatus.self) }
    }

    func setMemory(enabled: Bool) async {
        if let reply = await perform({ try await $0.send("PUT", "api/memory", fields: ["enabled": enabled], as: MemoryStatus.self) }) {
            memory = reply
        }
    }

    func saveNotes(_ notes: String) async {
        if let reply = await perform({ try await $0.send("PUT", "api/memory", fields: ["notes": notes], as: MemoryStatus.self) }) {
            memory = reply
        }
    }

    func searchMemory(_ query: String) async {
        if let reply = await perform({ try await $0.send("POST", "api/memory/search", fields: ["query": query], as: MemoryLines.self) }) {
            memoryLines = reply.lines
        }
    }

    func wipeMemory() async {
        if let reply = await perform({ try await $0.send("DELETE", "api/memory", as: MemoryStatus.self) }) {
            memory = reply
            memoryLines = []
        }
    }

    // MARK: Media

    func play(station: String) async {
        media = await perform { try await $0.send("POST", "api/media", fields: ["station": station], as: MediaStatus.self) } ?? media
    }

    func play(noise: String) async {
        media = await perform { try await $0.send("POST", "api/media", fields: ["noise": noise, "minutes": 60], as: MediaStatus.self) } ?? media
    }

    func stopMedia() async {
        media = await perform { try await $0.send("POST", "api/media", fields: [:], as: MediaStatus.self) } ?? media
    }

    // MARK: Chat

    func loadChat() async {
        guard !remote else {
            return
        }
        if let log = try? await client()?.send("GET", "api/chat", as: ChatLog.self) {
            chat = log.messages
            chatBusy = log.busy
        }
    }

    func sendChat(_ text: String) async -> Bool {
        let clean = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !clean.isEmpty else {
            return false
        }
        chatBusy = true
        if let log = await perform({ try await $0.send("POST", "api/chat", fields: ["text": clean], as: ChatLog.self) }) {
            chat = log.messages
            chatBusy = log.busy
            return true
        }
        chatBusy = false
        return false
    }

    func clearChat() async {
        if let log = await perform({ try await $0.send("DELETE", "api/chat", as: ChatLog.self) }) {
            chat = log.messages
        }
    }

    // MARK: Quiet and calendars

    func setQuiet(minutes: Int) async {
        _ = await perform { try await $0.send("POST", "api/quiet", fields: ["minutes": minutes], as: DeviceState.self) }
        await refresh()
    }

    func loadCalendars() async {
        calendars = await perform { try await $0.send("GET", "api/calendars", as: CalendarStatus.self) }
    }

    func setDevice(_ device: HomeDevice, fields: [String: Any]) async {
        if let list = await perform({ try await $0.send("PUT", "api/devices/\(device.id)", fields: fields, as: HomeDeviceList.self) }) {
            devices = list.devices
        }
    }

    func restart() async {
        if await perform({ try await $0.send("POST", "api/restart", fields: [:], as: Empty.self) }) != nil {
            restarting = true
        }
    }
}
