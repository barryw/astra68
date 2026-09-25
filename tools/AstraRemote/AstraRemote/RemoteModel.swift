import AppKit
import SwiftUI

@MainActor
final class RemoteModel: NSObject, ObservableObject, ARSessionDelegate {
    let session = ARSession()
    @Published var status = "Disconnected"
    @Published var message: String?
    @Published var image: NSImage?
    @Published var desktopSize = CGSize.zero
    @Published var soundEnabled = true {
        didSet { session.soundEnabled = soundEnabled }
    }

    var isConnected: Bool { status == "Connected" }
    var isBusy: Bool { status == "Connecting" }

    override init() {
        super.init()
        session.delegate = self
    }

    func connect(host: String, port: Int, password: String) {
        image = nil
        message = nil
        session.connect(toHost: host, port: port, password: password)
    }

    func disconnect() {
        session.disconnect()
        status = "Disconnecting"
    }

    func session(_ session: ARSession, didChangeStatus status: String, error: String?) {
        self.status = status
        self.message = error
        if status == "Disconnected" { image = nil }
    }

    func session(_ session: ARSession, didReceive image: NSImage, width: Int, height: Int) {
        desktopSize = CGSize(width: width, height: height)
        self.image = image
    }
}
