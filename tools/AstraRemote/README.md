# Astra Remote

Native macOS SwiftUI viewer for Astra's VNC desktop. The VNC connection and
QEMU audio extension are handled by TigerVNC's unmodified `rfbclient` library;
the window, input, and PCM playback are native macOS code.

## Build

Install Xcode, CMake, pixman, and libjpeg (`brew install cmake pixman jpeg-turbo`).
Open `AstraRemote.xcodeproj` and run the `AstraRemote` scheme. The first build
fetches a pinned TigerVNC source archive, verifies its SHA-256, and builds only
its static client libraries in Xcode's DerivedData. Subsequent builds reuse
that exact source. `project.yml` is the XcodeGen source for the checked-in
project; XcodeGen is not needed to open or build it. This build targets macOS
26 or newer; the app bundles the Homebrew image-decoding libraries it links.
The viewer remembers each host's VNC password in macOS Keychain, not in app
preferences. A Keychain access confirmation may appear on first use.

TigerVNC is GPL-2.0-or-later. Keep its license and source availability in mind
when distributing Astra Remote.

## Audio status

The client requests 48 kHz stereo signed-16-bit PCM through QEMU's VNC audio
extension and plays it through AVAudioEngine. The DE25 remote-desktop service
forwards the final host mixer output; its Linux audio tap drops packets rather
than blocking HDMI playback if a viewer is slow. Standard macOS Screen Sharing
does not request this extension and therefore remains silent.

The existing VNC server uses password authentication without transport
encryption. Use only on a trusted network until TLS is available.
