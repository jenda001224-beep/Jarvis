import Foundation

enum ClientError: LocalizedError {
    case unauthorized
    case busy
    case server(Int, String)
    case unreachable

    var errorDescription: String? {
        switch self {
        case .unauthorized: return "Telefon už není spárovaný."
        case .busy: return "Jarvis je právě zaneprázdněný."
        case .server(_, let message): return message
        case .unreachable: return "Displej není na dosah. Jsi na stejné Wi-Fi?"
        }
    }
}

struct JarvisClient {
    let base: URL
    let token: String?

    private static let session: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 6
        configuration.timeoutIntervalForResource = 12
        configuration.waitsForConnectivity = false
        return URLSession(configuration: configuration)
    }()

    private static let decoder: JSONDecoder = {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        return decoder
    }()

    private static let encoder: JSONEncoder = {
        let encoder = JSONEncoder()
        encoder.keyEncodingStrategy = .convertToSnakeCase
        return encoder
    }()

    func send<Reply: Decodable>(_ method: String, _ path: String, json: Data? = nil, as type: Reply.Type) async throws -> Reply {
        var request = URLRequest(url: base.appending(path: path))
        request.httpMethod = method
        if let token {
            request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }
        if let json {
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
            request.httpBody = json
        }
        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await JarvisClient.session.data(for: request)
        } catch {
            throw ClientError.unreachable
        }
        let status = (response as? HTTPURLResponse)?.statusCode ?? 0
        if status == 401 && path != "api/pair" {
            throw ClientError.unauthorized
        }
        if status == 409 {
            throw ClientError.busy
        }
        guard status == 200 else {
            let message = (try? JSONSerialization.jsonObject(with: data) as? [String: Any])?["error"] as? String
            throw ClientError.server(status, message ?? "Displej odpověděl chybou \(status).")
        }
        if Reply.self == Empty.self, data.isEmpty {
            return Empty() as! Reply
        }
        return try JarvisClient.decoder.decode(Reply.self, from: data)
    }

    func send<Reply: Decodable, Body: Encodable>(_ method: String, _ path: String, body: Body, as type: Reply.Type) async throws -> Reply {
        try await send(method, path, json: try JarvisClient.encoder.encode(body), as: type)
    }

    func send<Reply: Decodable>(_ method: String, _ path: String, fields: [String: Any], as type: Reply.Type) async throws -> Reply {
        try await send(method, path, json: try JSONSerialization.data(withJSONObject: fields), as: type)
    }
}
