import Foundation
import Network
import Observation

struct FoundDevice: Identifiable, Hashable {
    let id: String
    let name: String
    let endpoint: NWEndpoint
}

@MainActor
@Observable
final class Discovery {
    private(set) var devices: [FoundDevice] = []
    private(set) var searching = false
    private var browser: NWBrowser?

    func start() {
        stop()
        let parameters = NWParameters()
        parameters.includePeerToPeer = false
        let browser = NWBrowser(for: .bonjour(type: "_jarvis._tcp", domain: "local."), using: parameters)
        browser.browseResultsChangedHandler = { [weak self] results, _ in
            let found = results.compactMap { result -> FoundDevice? in
                guard case let .service(name, _, _, _) = result.endpoint else {
                    return nil
                }
                return FoundDevice(id: "\(result.endpoint)", name: name, endpoint: result.endpoint)
            }
            Task { @MainActor in
                self?.devices = found.sorted { $0.name < $1.name }
            }
        }
        browser.stateUpdateHandler = { [weak self] state in
            Task { @MainActor in
                switch state {
                case .ready: self?.searching = true
                case .failed, .cancelled: self?.searching = false
                default: break
                }
            }
        }
        browser.start(queue: .main)
        self.browser = browser
    }

    func stop() {
        browser?.cancel()
        browser = nil
        searching = false
    }

    static func address(of endpoint: NWEndpoint) async -> String? {
        await withCheckedContinuation { continuation in
            let connection = NWConnection(to: endpoint, using: .tcp)
            let gate = OnceGate()
            connection.stateUpdateHandler = { state in
                switch state {
                case .ready:
                    var address: String?
                    if case let .hostPort(host, port) = connection.currentPath?.remoteEndpoint {
                        var text = "\(host)"
                        if let percent = text.firstIndex(of: "%") {
                            text = String(text[..<percent])
                        }
                        address = text.contains(":") ? "[\(text)]:\(port)" : "\(text):\(port)"
                    }
                    connection.cancel()
                    if gate.open() { continuation.resume(returning: address) }
                case .failed, .cancelled:
                    if gate.open() { continuation.resume(returning: nil) }
                default:
                    break
                }
            }
            connection.start(queue: .global())
            DispatchQueue.global().asyncAfter(deadline: .now() + 6) {
                connection.cancel()
                if gate.open() { continuation.resume(returning: nil) }
            }
        }
    }
}

private final class OnceGate: @unchecked Sendable {
    private let lock = NSLock()
    private var used = false

    func open() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        if used { return false }
        used = true
        return true
    }
}
