import Foundation
import Security

@main
struct PasswordStoreCheck {
    static func main() {
        if CommandLine.arguments.count == 4 && CommandLine.arguments[1] == "--existing" {
            let host = CommandLine.arguments[2]
            let port = Int(CommandLine.arguments[3])!
            precondition(VNCPasswordStore.load(host: host, port: port) != nil)
            print("ASTRA_REMOTE_SAVED_PASSWORD PASS")
            return
        }
        let host = "astra-keychain-test-\(UUID().uuidString)"
        let port = 5900
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: VNCPasswordStore.service,
            kSecAttrAccount as String: VNCPasswordStore.account(host: host, port: port)
        ]
        defer { SecItemDelete(query as CFDictionary) }

        precondition(VNCPasswordStore.load(host: host, port: port) == nil)
        precondition(VNCPasswordStore.save("", host: host, port: port) == errSecParam)
        let firstStatus = VNCPasswordStore.save("first", host: host, port: port)
        precondition(firstStatus == errSecSuccess, "Keychain save failed: \(firstStatus)")
        precondition(VNCPasswordStore.load(host: host.uppercased(), port: port) == "first")
        precondition(VNCPasswordStore.load(host: host, port: port + 1) == nil)
        precondition(VNCPasswordStore.save("second", host: host, port: port) == errSecSuccess)
        precondition(VNCPasswordStore.load(host: host, port: port) == "second")
        print("ASTRA_REMOTE_KEYCHAIN PASS")
    }
}
