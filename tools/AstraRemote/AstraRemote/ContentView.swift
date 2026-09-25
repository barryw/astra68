import SwiftUI
import Security

struct ContentView: View {
    @ObservedObject var remote: RemoteModel
    @AppStorage("astraRemoteHost") private var host = "192.168.1.52"
    @AppStorage("astraRemotePort") private var port = "5900"
    @State private var password = ""
    @State private var keychainError: String?
    @FocusState private var focusedField: Field?

    private enum Field { case host, password }

    var body: some View {
        Group {
            if remote.isConnected, let image = remote.image {
                RemoteCanvas(session: remote.session, image: image,
                             desktopSize: remote.desktopSize)
                    .background(.black)
            } else {
                connectionForm
            }
        }
        .onAppear(perform: loadPassword)
        .onChange(of: "\(host):\(port)") { _, _ in loadPassword() }
        .toolbar {
            ToolbarItem(placement: .principal) {
                HStack(spacing: 7) {
                    Circle()
                        .fill(remote.isConnected ? .green : remote.isBusy ? .orange : .secondary)
                        .frame(width: 7, height: 7)
                    Text(remote.isConnected ? host : "Astra Remote")
                        .lineLimit(1)
                }
                .accessibilityLabel("Connection status: \(remote.status)")
            }
            ToolbarItemGroup(placement: .primaryAction) {
                if remote.isConnected {
                    Toggle("Sound", isOn: $remote.soundEnabled)
                        .toggleStyle(.button)
                        .labelStyle(.iconOnly)
                        .help(remote.soundEnabled ? "Mute remote sound" : "Unmute remote sound")
                    Button("Disconnect", systemImage: "network.slash") {
                        remote.disconnect()
                    }
                    .help("Disconnect")
                }
            }
        }
    }

    private var connectionForm: some View {
        VStack(spacing: 0) {
            Spacer()
            VStack(alignment: .leading, spacing: 24) {
                HStack(alignment: .center, spacing: 18) {
                    Image(systemName: "desktopcomputer")
                        .font(.system(size: 38, weight: .light))
                        .foregroundStyle(.tint)
                        .frame(width: 62, height: 62)
                        .background(.quaternary, in: RoundedRectangle(cornerRadius: 16))
                    VStack(alignment: .leading, spacing: 5) {
                        Text("Connect to Astra")
                            .font(.title2.weight(.semibold))
                        Text("Your desktop, right here on your Mac.")
                            .foregroundStyle(.secondary)
                    }
                }

                Form {
                    TextField("Host", text: $host)
                        .textContentType(.URL)
                        .focused($focusedField, equals: .host)
                    TextField("Port", text: $port)
                        .frame(maxWidth: 120, alignment: .leading)
                    SecureField("Password", text: $password)
                        .focused($focusedField, equals: .password)
                        .onSubmit(connect)
                }
                .formStyle(.grouped)

                if let message = remote.message {
                    Label(message, systemImage: "exclamationmark.triangle")
                        .foregroundStyle(.red)
                        .font(.callout)
                        .fixedSize(horizontal: false, vertical: true)
                }
                if let keychainError {
                    Label(keychainError, systemImage: "key.slash")
                        .foregroundStyle(.red)
                        .font(.callout)
                }

                HStack {
                    Text(remote.status == "Disconnected" ?
                         "VNC password authentication is not encrypted." : remote.status)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer()
                    Button("Connect", action: connect)
                        .buttonStyle(.borderedProminent)
                        .disabled(remote.isBusy || remote.status == "Disconnecting")
                        .keyboardShortcut(.return, modifiers: [])
                }
            }
            .frame(width: 440)
            Spacer()
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private func connect() {
        let address = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !address.isEmpty, let number = Int(port), (1...65535).contains(number) else {
            remote.message = "Enter a host and a port from 1 to 65535."
            return
        }
        guard !password.isEmpty else {
            remote.message = "Enter the VNC password."
            return
        }
        let status = VNCPasswordStore.save(password, host: address, port: number)
        keychainError = status == errSecSuccess ? nil :
            "Could not save the password in Keychain (\(status))."
        remote.connect(host: address, port: number, password: password)
    }

    private func loadPassword() {
        guard let number = Int(port), (1...65535).contains(number) else {
            password = ""
            return
        }
        password = VNCPasswordStore.load(host: host, port: number) ?? ""
    }
}
