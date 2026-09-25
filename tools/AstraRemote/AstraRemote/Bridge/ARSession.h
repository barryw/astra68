#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

@class ARSession;

__attribute__((swift_attr("@MainActor")))
@protocol ARSessionDelegate <NSObject>
- (void)session:(ARSession *)session didChangeStatus:(NSString *)status error:(nullable NSString *)error;
- (void)session:(ARSession *)session didReceiveImage:(NSImage *)image
          width:(NSInteger)width height:(NSInteger)height;
@end

@interface ARSession : NSObject
@property (nonatomic, weak, nullable) id<ARSessionDelegate> delegate;
@property (atomic) BOOL soundEnabled;
- (void)connectToHost:(NSString *)host port:(NSInteger)port password:(NSString *)password;
- (void)disconnect;
- (void)sendPointerX:(NSInteger)x y:(NSInteger)y buttons:(NSUInteger)buttons;
- (void)sendKeyCode:(NSInteger)code keysym:(uint32_t)keysym pressed:(BOOL)pressed;
- (void)releaseKeys;
@end

NS_ASSUME_NONNULL_END
