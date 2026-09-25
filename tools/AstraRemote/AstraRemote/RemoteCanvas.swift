import AppKit
import SwiftUI

struct RemoteCanvas: NSViewRepresentable {
    let session: ARSession
    let image: NSImage
    let desktopSize: CGSize

    func makeNSView(context: Context) -> RemoteDisplayView {
        let view = RemoteDisplayView()
        view.session = session
        return view
    }

    func updateNSView(_ view: RemoteDisplayView, context: Context) {
        view.image = image
        view.desktopSize = desktopSize
        view.needsDisplay = true
    }
}

final class RemoteDisplayView: NSView {
    var session: ARSession?
    var image: NSImage?
    var desktopSize = CGSize.zero
    private var buttons: UInt = 0
    private var pressedModifiers = Set<UInt16>()
    private var tracking: NSTrackingArea?

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let tracking { removeTrackingArea(tracking) }
        tracking = NSTrackingArea(rect: .zero, options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect],
                                  owner: self, userInfo: nil)
        addTrackingArea(tracking!)
    }

    private var imageRect: NSRect {
        guard desktopSize.width > 0, desktopSize.height > 0 else { return .zero }
        let scale = min(bounds.width / desktopSize.width, bounds.height / desktopSize.height)
        let size = CGSize(width: desktopSize.width * scale, height: desktopSize.height * scale)
        return NSRect(x: (bounds.width - size.width) / 2,
                      y: (bounds.height - size.height) / 2,
                      width: size.width, height: size.height)
    }

    override func draw(_ dirtyRect: NSRect) {
        NSColor.black.setFill()
        bounds.fill()
        guard let image else { return }
        NSGraphicsContext.current?.imageInterpolation = .none
        image.draw(in: imageRect)
    }

    private func sendPointer(_ event: NSEvent, buttons mask: UInt? = nil) {
        let rect = imageRect
        guard rect.width > 0, rect.height > 0 else { return }
        let point = convert(event.locationInWindow, from: nil)
        let x = Int(((point.x - rect.minX) / rect.width * desktopSize.width).rounded(.down))
        let y = Int(((point.y - rect.minY) / rect.height * desktopSize.height).rounded(.down))
        session?.sendPointerX(max(0, min(x, Int(desktopSize.width) - 1)),
                              y: max(0, min(y, Int(desktopSize.height) - 1)),
                              buttons: mask ?? buttons)
    }

    override func mouseMoved(with event: NSEvent) { sendPointer(event) }
    override func mouseDragged(with event: NSEvent) { sendPointer(event) }
    override func rightMouseDragged(with event: NSEvent) { sendPointer(event) }
    override func otherMouseDragged(with event: NSEvent) { sendPointer(event) }
    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        buttons |= 1
        sendPointer(event)
    }
    override func mouseUp(with event: NSEvent) { buttons &= ~UInt(1); sendPointer(event) }
    override func rightMouseDown(with event: NSEvent) { buttons |= 4; sendPointer(event) }
    override func rightMouseUp(with event: NSEvent) { buttons &= ~UInt(4); sendPointer(event) }
    override func otherMouseDown(with event: NSEvent) { buttons |= 2; sendPointer(event) }
    override func otherMouseUp(with event: NSEvent) { buttons &= ~UInt(2); sendPointer(event) }

    override func scrollWheel(with event: NSEvent) {
        if event.scrollingDeltaY != 0 {
            let wheel: UInt = event.scrollingDeltaY > 0 ? 8 : 16
            sendPointer(event, buttons: buttons | wheel)
            sendPointer(event)
        }
        if event.scrollingDeltaX != 0 {
            let wheel: UInt = event.scrollingDeltaX > 0 ? 32 : 64
            sendPointer(event, buttons: buttons | wheel)
            sendPointer(event)
        }
    }

    override func keyDown(with event: NSEvent) {
        guard let symbol = Self.keysym(for: event) else { return }
        session?.sendKeyCode(Int(event.keyCode), keysym: symbol, pressed: true)
    }
    override func keyUp(with event: NSEvent) {
        session?.sendKeyCode(Int(event.keyCode), keysym: 0, pressed: false)
    }
    override func flagsChanged(with event: NSEvent) {
        guard let symbol = Self.modifierSymbols[event.keyCode] else { return }
        let down = !pressedModifiers.contains(event.keyCode)
        if down { pressedModifiers.insert(event.keyCode) }
        else { pressedModifiers.remove(event.keyCode) }
        session?.sendKeyCode(Int(event.keyCode), keysym: symbol, pressed: down)
    }
    override func resignFirstResponder() -> Bool {
        pressedModifiers.removeAll()
        session?.releaseKeys()
        return super.resignFirstResponder()
    }

    private static let modifierSymbols: [UInt16: UInt32] = [
        56: 0xffe1, 60: 0xffe2, 59: 0xffe3, 62: 0xffe4,
        58: 0xffe9, 61: 0xffea, 55: 0xffeb, 54: 0xffec
    ]
    private static let specialSymbols: [UInt16: UInt32] = [
        36: 0xff0d, 48: 0xff09, 49: 0x20, 51: 0xff08, 53: 0xff1b,
        117: 0xffff, 123: 0xff51, 124: 0xff53, 125: 0xff54, 126: 0xff52,
        115: 0xff50, 119: 0xff57, 116: 0xff55, 121: 0xff56,
        122: 0xffbe, 120: 0xffbf, 99: 0xffc0, 118: 0xffc1,
        96: 0xffc2, 97: 0xffc3, 98: 0xffc4, 100: 0xffc5,
        101: 0xffc6, 109: 0xffc7, 103: 0xffc8, 111: 0xffc9
    ]

    static func keysym(for event: NSEvent) -> UInt32? {
        if let special = specialSymbols[event.keyCode] { return special }
        let text = event.modifierFlags.contains(.control) ?
            event.charactersIgnoringModifiers : event.characters
        guard let scalar = text?.unicodeScalars.first else { return nil }
        let value = scalar.value
        return value <= 0xff ? value : 0x01000000 | value
    }
}
