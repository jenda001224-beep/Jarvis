import Foundation

struct Hello: Decodable {
    let name: String
    let model: String
    let version: String
    let paired: Bool
}

struct PairReply: Decodable {
    let token: String
}

struct Network: Decodable {
    let ssid: String
    let rssi: Int
    let ip: String
}

struct Memory: Decodable {
    let `internal`: Int
    let external: Int
}

struct Prefs: Codable, Equatable {
    var volume: Int
    var brightness: Int
    var sleep: Int
    var dark: Int
    var orientation: Int
    var sensitivity: Int
    var night: Bool
    var nightFrom: Int
    var nightTo: Int
    var announce: Int?
}

struct StoredSettings: Decodable {
    let voice: String
    let place: String
    let home: String
    let transport: String
    let search: Bool
    let searchKey: Bool
    let printer: Bool
    let calendars: Int
    let apple: Bool?
}

struct CloudStatus: Decodable {
    let configured: Bool
    let problem: String
}

struct Hub: Decodable {
    let state: String
    let devices: Int
}

struct NextAlarm: Decodable {
    let id: Int
    let at: Double
    let label: String

    var date: Date { Date(timeIntervalSince1970: at) }
}

struct DeviceState: Decodable {
    let name: String
    let model: String
    let version: String
    let state: String
    let uptime: Int
    let muted: Bool
    let lamp: Bool
    let revision: Int
    let network: Network
    let memory: Memory
    let prefs: Prefs
    let settings: StoredSettings
    let hub: Hub
    let todosOpen: Int
    let nextAlarm: NextAlarm?
    let quietUntil: Double?
    let away: Bool?
    let cloud: CloudStatus?

    var quietEnd: Date? {
        guard let quietUntil, quietUntil > 0 else {
            return nil
        }
        return Date(timeIntervalSince1970: quietUntil)
    }
}

struct Todo: Codable, Identifiable, Equatable {
    let id: Int
    var text: String
    var done: Bool
    let created: Double
}

struct Alarm: Codable, Identifiable, Equatable {
    let id: Int
    var hour: Int
    var minute: Int
    var days: Int
    var label: String
    var enabled: Bool

    static let weekdays = 0x1F
    static let everyDay = 0x7F

    var timeText: String { String(format: "%d:%02d", hour, minute) }

    var repeatText: String {
        switch days {
        case 0: return "Jednou"
        case Alarm.everyDay: return "Každý den"
        case Alarm.weekdays: return "Všední dny"
        case 0x60: return "Víkend"
        default:
            return Weekday.all.filter { days & $0.bit != 0 }.map(\.short).joined(separator: " ")
        }
    }
}

struct Weekday: Identifiable {
    let id: Int
    let short: String
    let name: String

    var bit: Int { 1 << id }

    static let all = [
        Weekday(id: 0, short: "Po", name: "pondělí"),
        Weekday(id: 1, short: "Út", name: "úterý"),
        Weekday(id: 2, short: "St", name: "středa"),
        Weekday(id: 3, short: "Čt", name: "čtvrtek"),
        Weekday(id: 4, short: "Pá", name: "pátek"),
        Weekday(id: 5, short: "So", name: "sobota"),
        Weekday(id: 6, short: "Ne", name: "neděle"),
    ]
}

struct AlarmDraft: Encodable {
    var hour: Int
    var minute: Int
    var days: Int
    var label: String
    var enabled: Bool
}

struct Reminder: Decodable, Identifiable {
    let id: Int
    let due: Double
    let text: String

    var date: Date { Date(timeIntervalSince1970: due) }
}

struct TodoList: Decodable {
    let todos: [Todo]
}

struct AlarmList: Decodable {
    let alarms: [Alarm]
}

struct ReminderList: Decodable {
    let reminders: [Reminder]
}

struct PrefsReply: Decodable {
    let prefs: Prefs
}

struct Empty: Decodable {}

enum SleepChoice: Int, CaseIterable, Identifiable {
    case halfMinute, minute, fiveMinutes, quarterHour, never

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .halfMinute: return "30 s"
        case .minute: return "1 min"
        case .fiveMinutes: return "5 min"
        case .quarterHour: return "15 min"
        case .never: return "Nikdy"
        }
    }
}

enum Voice: String, CaseIterable, Identifiable {
    case charon = "Charon", orus = "Orus", puck = "Puck", fenrir = "Fenrir"
    case kore = "Kore", aoede = "Aoede", leda = "Leda", zephyr = "Zephyr"

    var id: String { rawValue }

    var title: String {
        switch self {
        case .charon: return "Charon — věcný"
        case .orus: return "Orus — pevný"
        case .puck: return "Puck — živý"
        case .fenrir: return "Fenrir — vznětlivý"
        case .kore: return "Kore — rozhodný"
        case .aoede: return "Aoede — vzdušný"
        case .leda: return "Leda — mladistvý"
        case .zephyr: return "Zephyr — jasný"
        }
    }
}

enum Transport: String, CaseIterable, Identifiable {
    case scooter, bike, walk, car

    var id: String { rawValue }

    var title: String {
        switch self {
        case .scooter: return "Elektrokoloběžka"
        case .bike: return "Kolo"
        case .walk: return "Pěšky"
        case .car: return "Auto"
        }
    }
}

struct HomeDevice: Decodable, Identifiable {
    let id: String
    let name: String
    let room: String
    let hubName: String
    let hubRoom: String
    let light: Bool
    let on: Bool
    let reachable: Bool
    let hidden: Bool?
    let welcome: Bool?

    var renamed: Bool { name != hubName || room != hubRoom }
}

struct HomeDeviceList: Decodable {
    let devices: [HomeDevice]
}

struct TimerItem: Decodable, Identifiable {
    let id: Int
    let label: String
    let seconds: Int
    let remaining: Int
    let left: String
}

struct TimerList: Decodable {
    let timers: [TimerItem]
}

struct Notice: Decodable, Identifiable {
    let id: Int
    let when: Double
    let source: String
    let text: String
    let read: Bool

    var date: Date { Date(timeIntervalSince1970: when) }
}

struct NoticeList: Decodable {
    let unread: Int
    let notices: [Notice]
}

struct MemoryStatus: Decodable {
    let enabled: Bool
    let available: Bool
    let notes: String
    let lines: Int
}

struct MemoryLine: Decodable, Identifiable {
    let when: String
    let unix: Double
    let who: String
    let text: String

    var id: String { "\(unix)-\(who)-\(text.prefix(24))" }
}

struct MemoryLines: Decodable {
    let lines: [MemoryLine]
}

struct MediaStatus: Decodable {
    let title: String
    let playing: Bool
    let stations: [String]
}

struct ChatMessage: Decodable, Identifiable {
    let seq: Int
    let when: Double
    let mine: Bool
    let text: String

    var id: Int { seq }
}

struct ChatLog: Decodable {
    let busy: Bool
    let messages: [ChatMessage]
}

struct CalendarStatus: Decodable {
    let configured: Bool
    let calendars: [String]
    let error: String?
    let appleId: String?
    let chosen: String?
}

struct Snapshot: Decodable {
    let state: DeviceState
    let todos: [Todo]?
    let alarms: [Alarm]?
    let timers: [TimerItem]?
    let chat: ChatLog?
    let media: MediaStatus?

    private enum CodingKeys: String, CodingKey {
        case state, todos, alarms, timers, chat, media
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        state = try container.decode(DeviceState.self, forKey: .state)
        todos = try? container.decode(TodoList.self, forKey: .todos).todos
        alarms = try? container.decode(AlarmList.self, forKey: .alarms).alarms
        timers = try? container.decode(TimerList.self, forKey: .timers).timers
        chat = try? container.decode(ChatLog.self, forKey: .chat)
        media = try? container.decode(MediaStatus.self, forKey: .media)
    }
}
