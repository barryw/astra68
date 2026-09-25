#import "ARSession.h"

#import <AVFAudio/AVFAudio.h>
#import <CoreGraphics/CoreGraphics.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>

#include <poll.h>

#include <network/TcpSocket.h>
#include <rdr/FdInStream.h>
#include <rdr/FdOutStream.h>
#include <rfb/CConnection.h>
#include <rfb/CMsgWriter.h>
#include <rfb/PixelBuffer.h>
#include <rfb/encodings.h>

@interface ARPCMOutput : NSObject
- (void)begin;
- (void)end;
- (void)writeBytes:(const uint8_t *)bytes length:(size_t)length;
@end

@implementation ARPCMOutput {
    AVAudioEngine *_engine;
    AVAudioPlayerNode *_player;
    AVAudioFormat *_format;
    NSMutableData *_pending;
    std::atomic<uint32_t> _queuedFrames;
    std::atomic<uint64_t> _generation;
    BOOL _active;
    BOOL _receivedAudio;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _engine = [AVAudioEngine new];
        _player = [AVAudioPlayerNode new];
        _format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000 channels:2];
        _pending = [NSMutableData data];
        [_engine attachNode:_player];
        [_engine connect:_player to:_engine.mainMixerNode format:_format];
    }
    return self;
}

- (void)begin {
    [self end];
    NSError *error = nil;
    if (![_engine startAndReturnError:&error]) {
        NSLog(@"Astra Remote audio unavailable: %@", error);
        return;
    }
    [_player play];
    _active = YES;
    _receivedAudio = NO;
    NSLog(@"Astra Remote audio: 48 kHz stereo playback started");
}

- (void)end {
    _active = NO;
    ++_generation;
    [_player stop];
    [_engine stop];
    [_pending setLength:0];
    _queuedFrames = 0;
}

- (void)writeBytes:(const uint8_t *)bytes length:(size_t)length {
    if (!_active || length == 0) return;
    if (!_receivedAudio) {
        _receivedAudio = YES;
        NSLog(@"Astra Remote audio: first PCM packet received (%zu bytes)", length);
    }
    [_pending appendBytes:bytes length:length];
    const uint8_t *input = (const uint8_t *)_pending.bytes;
    size_t available = _pending.length / 4;
    size_t consumed = 0;

    // Bound playback latency if the network delivers a burst faster than audio plays.
    if (_queuedFrames > 24000) {
        ++_generation;
        [_player stop];
        [_player play];
        _queuedFrames = 0;
    }
    while (available > 0) {
        AVAudioFrameCount count = (AVAudioFrameCount)MIN(available, (size_t)960);
        AVAudioPCMBuffer *buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:_format
                                                                 frameCapacity:count];
        if (!buffer) break;
        buffer.frameLength = count;
        float *left = buffer.floatChannelData[0];
        float *right = buffer.floatChannelData[1];
        for (AVAudioFrameCount frame = 0; frame < count; ++frame) {
            size_t offset = (consumed + frame) * 4;
            int16_t l = (int16_t)((uint16_t)input[offset] | ((uint16_t)input[offset + 1] << 8));
            int16_t r = (int16_t)((uint16_t)input[offset + 2] | ((uint16_t)input[offset + 3] << 8));
            left[frame] = (float)l / 32768.0f;
            right[frame] = (float)r / 32768.0f;
        }
        uint64_t generation = _generation.load();
        _queuedFrames.fetch_add(count);
        [_player scheduleBuffer:buffer completionHandler:^{
            if (generation == self->_generation.load()) self->_queuedFrames.fetch_sub(count);
        }];
        consumed += count;
        available -= count;
    }
    if (consumed) [_pending replaceBytesInRange:NSMakeRange(0, consumed * 4) withBytes:NULL length:0];
}
@end

@interface ARSession ()
- (void)publishStatus:(NSString *)status error:(NSString * _Nullable)error;
- (void)publishFramebuffer:(const rfb::PixelBuffer *)framebuffer;
@end

class ARConnection final : public rfb::CConnection {
public:
    ARConnection(ARSession *owner, NSString *password)
        : owner_(owner), password_([password UTF8String]), audio_([ARPCMOutput new]) {
        setShared(true);
        setPreferredEncoding(rfb::encodingTight);
        supportsAudio = true;
    }

    void pointer(int x, int y, unsigned buttons) {
        writer()->writePointerEvent(core::Point(x, y), buttons);
    }

    void initDone() override {
        const rfb::PixelFormat bgra(32, 24, false, true, 255, 255, 255, 16, 8, 0);
        setFramebuffer(new rfb::ManagedPixelBuffer(bgra, server.width(), server.height()));
        setPF(bgra);
        [owner_ publishStatus:@"Connected" error:nil];
    }

    void bell() override { dispatch_async(dispatch_get_main_queue(), ^{ NSBeep(); }); }

    void resizeFramebuffer() override {
        const rfb::PixelFormat bgra(32, 24, false, true, 255, 255, 255, 16, 8, 0);
        setFramebuffer(new rfb::ManagedPixelBuffer(bgra, server.width(), server.height()));
    }

    void framebufferUpdateEnd() override {
        CConnection::framebufferUpdateEnd();
        [owner_ publishFramebuffer:getFramebuffer()];
    }

    void getUserPasswd(bool, std::string *user, std::string *password) override {
        if (user) *user = "";
        *password = password_;
    }

    bool verifyCertificate(unsigned int, const uint8_t *, size_t) override { return false; }
    bool verifyHostKey(const uint8_t *, size_t, const char *) override { return false; }
    void handleAudioBegin() override { [audio_ begin]; }
    void handleAudioEnd() override { [audio_ end]; }
    void handleAudioData(const uint8_t *bytes, size_t length) override {
        if (owner_.soundEnabled) [audio_ writeBytes:bytes length:length];
    }

private:
    __weak ARSession *owner_;
    std::string password_;
    ARPCMOutput *audio_;
};

@implementation ARSession {
    std::atomic<bool> _stopping;
    std::mutex _commandLock;
    std::deque<std::function<void(ARConnection &)>> _commands;
    BOOL _lastCommandWasPointer;
    NSUInteger _lastPointerButtons;
    std::mutex _frameLock;
    CGImageRef _latestFrame;
    BOOL _frameScheduled;
    BOOL _running;
}

- (instancetype)init {
    self = [super init];
    if (self) _soundEnabled = YES;
    return self;
}

- (void)dealloc {
    if (_latestFrame) CGImageRelease(_latestFrame);
}

- (void)publishStatus:(NSString *)status error:(NSString *)error {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.delegate session:self didChangeStatus:status error:error];
    });
}

- (void)publishFramebuffer:(const rfb::PixelBuffer *)framebuffer {
    if (!framebuffer) return;
    int width = framebuffer->width(), height = framebuffer->height(), stride = 0;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return;
    const uint8_t *pixels = framebuffer->getBuffer(core::Rect(0, 0, width, height), &stride);
    if (!pixels || stride < width || (size_t)stride > SIZE_MAX / 4 / (size_t)height) return;
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, pixels, (CFIndex)((size_t)stride * 4 * height));
    if (!data) return;
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CFRelease(data);
    if (!provider) return;
    CGColorSpaceRef colors = CGColorSpaceCreateDeviceRGB();
    CGImageRef image = CGImageCreate(width, height, 8, 32, stride * 4, colors,
                                    kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst,
                                    provider, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(colors);
    CGDataProviderRelease(provider);
    if (!image) return;
    BOOL schedule = NO;
    {
        std::lock_guard<std::mutex> lock(_frameLock);
        if (_latestFrame) CGImageRelease(_latestFrame);
        _latestFrame = image;
        if (!_frameScheduled) {
            _frameScheduled = YES;
            schedule = YES;
        }
    }
    if (!schedule) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        CGImageRef newest;
        {
            std::lock_guard<std::mutex> lock(self->_frameLock);
            newest = self->_latestFrame;
            self->_latestFrame = NULL;
            self->_frameScheduled = NO;
        }
        if (!newest) return;
        NSInteger latestWidth = CGImageGetWidth(newest);
        NSInteger latestHeight = CGImageGetHeight(newest);
        NSImage *display = [[NSImage alloc] initWithCGImage:newest
                                                       size:NSMakeSize(latestWidth, latestHeight)];
        [self.delegate session:self didReceiveImage:display
                          width:latestWidth height:latestHeight];
        CGImageRelease(newest);
    });
}

- (void)connectToHost:(NSString *)host port:(NSInteger)port password:(NSString *)password {
    @synchronized (self) {
        if (_running) return;
        _running = YES;
        _stopping = false;
    }
    [self publishStatus:@"Connecting" error:nil];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            try {
                network::TcpSocket socket(host.UTF8String, (int)port);
                ARConnection connection(self, password);
                connection.setServerName(host.UTF8String);
                connection.setStreams(&socket.inStream(), &socket.outStream());
                connection.initialiseProtocol();
                while (!self->_stopping.load()) {
                    std::deque<std::function<void(ARConnection &)>> commands;
                    {
                        std::lock_guard<std::mutex> lock(self->_commandLock);
                        commands.swap(self->_commands);
                        self->_lastCommandWasPointer = NO;
                    }
                    for (const auto &command : commands) command(connection);
                    if (!commands.empty()) socket.outStream().flush();
                    struct pollfd fd = {socket.getFd(), POLLIN, 0};
                    int ready = poll(&fd, 1, 16);
                    if (ready < 0 && errno == EINTR) continue;
                    if (ready < 0) throw std::runtime_error("VNC socket poll failed");
                    if (fd.revents & (POLLERR | POLLHUP | POLLNVAL))
                        throw std::runtime_error("VNC connection closed");
                    if (ready) {
                        while (!self->_stopping.load()) {
                            bool consumed;
                            @autoreleasepool { consumed = connection.processMsg(); }
                            if (!consumed) break;
                        }
                    }
                }
                connection.close();
            } catch (const std::exception &error) {
                if (!self->_stopping.load())
                    [self publishStatus:@"Disconnected"
                                   error:[NSString stringWithUTF8String:error.what()]];
            }
            {
                std::lock_guard<std::mutex> lock(self->_commandLock);
                self->_commands.clear();
            }
            @synchronized (self) { self->_running = NO; }
            if (self->_stopping.load()) [self publishStatus:@"Disconnected" error:nil];
        }
    });
}

- (void)disconnect { _stopping = true; }

- (void)sendPointerX:(NSInteger)x y:(NSInteger)y buttons:(NSUInteger)buttons {
    std::lock_guard<std::mutex> lock(_commandLock);
    std::function<void(ARConnection &)> command = [=](ARConnection &connection) {
        connection.pointer((int)x, (int)y, (unsigned)buttons);
    };
    if (_lastCommandWasPointer && _lastPointerButtons == buttons && !_commands.empty())
        _commands.back() = std::move(command);
    else
        _commands.emplace_back(std::move(command));
    _lastCommandWasPointer = YES;
    _lastPointerButtons = buttons;
}

- (void)sendKeyCode:(NSInteger)code keysym:(uint32_t)keysym pressed:(BOOL)pressed {
    std::lock_guard<std::mutex> lock(_commandLock);
    _lastCommandWasPointer = NO;
    _commands.emplace_back([=](ARConnection &connection) {
        if (pressed) connection.sendKeyPress((int)code, 0, keysym);
        else connection.sendKeyRelease((int)code);
    });
}

- (void)releaseKeys {
    std::lock_guard<std::mutex> lock(_commandLock);
    _lastCommandWasPointer = NO;
    _commands.emplace_back([](ARConnection &connection) { connection.releaseAllKeys(); });
}
@end
