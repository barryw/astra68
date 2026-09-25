import Foundation
import Security

enum VNCPasswordStore {
    static let service = "org.astra68.AstraRemote.vnc"

    static func account(host: String, port: Int) -> String {
        "\(host.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()):\(port)"
    }

    static func load(host: String, port: Int) -> String? {
        var query = baseQuery(host: host, port: port)
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &result) == errSecSuccess,
              let data = result as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    @discardableResult
    static func save(_ password: String, host: String, port: Int) -> OSStatus {
        guard !password.isEmpty else { return errSecParam }
        let query = baseQuery(host: host, port: port)
        let value = [kSecValueData as String: Data(password.utf8)]
        var item = query
        item[kSecValueData as String] = Data(password.utf8)
        let status = SecItemAdd(item as CFDictionary, nil)
        return status == errSecDuplicateItem ?
            SecItemUpdate(query as CFDictionary, value as CFDictionary) : status
    }

    private static func baseQuery(host: String, port: Int) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: service,
         kSecAttrAccount as String: account(host: host, port: port)]
    }
}
