import CloudKit
import CryptoKit
import Foundation

actor CloudMail {
    static let containerName = "iCloud.com.jenda.jarvis"
    private static let mailType = "JarvisMail"
    private static let noticeType = "JarvisNotice"
    private static let subscription = "jarvis-notices"

    private let database: CKDatabase
    private let key: SymmetricKey
    private let commandID: CKRecord.ID
    private let replyID: CKRecord.ID
    private let snapshotID: CKRecord.ID
    private var sequence = Int64(Date().timeIntervalSince1970 * 10)
    private var sending = false
    private var waiting: [CheckedContinuation<Void, Never>] = []
    private var subscribed = false

    init(token: String) {
        database = CKContainer(identifier: CloudMail.containerName).publicCloudDatabase
        key = SymmetricKey(data: SHA256.hash(data: Data(("jarvis-cloud" + token).utf8)))
        let phone = SHA256.hash(data: Data(token.utf8)).prefix(8).map { String(format: "%02x", $0) }.joined()
        commandID = CKRecord.ID(recordName: "cmd-\(phone)")
        replyID = CKRecord.ID(recordName: "rep-\(phone)")
        snapshotID = CKRecord.ID(recordName: "snap-\(phone)")
    }

    private func acquire() async {
        if !sending {
            sending = true
            return
        }
        await withCheckedContinuation { waiting.append($0) }
    }

    private func release() {
        if waiting.isEmpty {
            sending = false
        } else {
            waiting.removeFirst().resume()
        }
    }

    private func seal(_ plain: Data) throws -> String {
        guard let combined = try AES.GCM.seal(plain, using: key).combined else {
            throw ClientError.unreachable
        }
        return combined.base64EncodedString()
    }

    private func open(_ text: String) throws -> Data {
        guard let raw = Data(base64Encoded: text) else {
            throw ClientError.unreachable
        }
        return try AES.GCM.open(AES.GCM.SealedBox(combined: raw), using: key)
    }

    func send(method: String, path: String, body: Data?) async throws -> (Int, Data) {
        await acquire()
        defer { release() }
        sequence += 1
        let mine = sequence
        var command: [String: Any] = ["method": method, "path": path]
        if let body {
            command["body"] = String(decoding: body, as: UTF8.self)
        }
        let record = CKRecord(recordType: CloudMail.mailType, recordID: commandID)
        record["seq"] = mine
        record["data"] = try seal(try JSONSerialization.data(withJSONObject: command))
        _ = try await database.modifyRecords(saving: [record], deleting: [], savePolicy: .allKeys)
        for _ in 0..<40 {
            try await Task.sleep(nanoseconds: 1_500_000_000)
            guard let reply = try? await database.record(for: replyID), (reply["seq"] as? Int64) == mine,
                  let text = reply["data"] as? String else {
                continue
            }
            let plain = try open(text)
            guard let answer = try JSONSerialization.jsonObject(with: plain) as? [String: Any] else {
                throw ClientError.unreachable
            }
            let status = answer["status"] as? Int ?? 500
            return (status, Data((answer["reply"] as? String ?? "").utf8))
        }
        throw ClientError.unreachable
    }

    func snapshot() async throws -> Data {
        await subscribeIfNeeded()
        let record = try await database.record(for: snapshotID)
        guard let text = record["data"] as? String else {
            throw ClientError.unreachable
        }
        return try open(text)
    }

    func subscribeIfNeeded() async {
        guard !subscribed else {
            return
        }
        let info = CKSubscription.NotificationInfo()
        info.titleLocalizationKey = "Jarvis"
        info.alertLocalizationKey = "%1$@"
        info.alertLocalizationArgs = ["text"]
        info.soundName = "default"
        let subscription = CKQuerySubscription(recordType: CloudMail.noticeType, predicate: NSPredicate(value: true),
                                               subscriptionID: CloudMail.subscription, options: [.firesOnRecordCreation])
        subscription.notificationInfo = info
        do {
            _ = try await database.save(subscription)
            subscribed = true
        } catch {
            let schema = CKRecord(recordType: CloudMail.noticeType, recordID: CKRecord.ID(recordName: "notice-schema"))
            schema["seq"] = Int64(0)
            schema["text"] = ""
            _ = try? await database.modifyRecords(saving: [schema], deleting: [], savePolicy: .allKeys)
        }
    }
}
