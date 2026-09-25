import SwiftUI

@main
struct AstraRemoteApp: App {
    @StateObject private var remote = RemoteModel()

    var body: some Scene {
        WindowGroup("Astra Remote") {
            ContentView(remote: remote)
                .frame(minWidth: 640, minHeight: 420)
        }
        .defaultSize(width: 1280, height: 800)
        .commands {
            CommandGroup(after: .newItem) {
                Button("Disconnect") { remote.disconnect() }
                    .keyboardShortcut("k", modifiers: [.command, .shift])
                    .disabled(!remote.isConnected)
            }
        }
    }
}
