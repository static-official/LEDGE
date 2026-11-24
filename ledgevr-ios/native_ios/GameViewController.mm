// LEDGE iOS — game view controller: owns the CAEAGLLayer, drives the native
// game loop with CADisplayLink, forwards UITouches into ios_backend.
#import "GameViewController.h"
#import <QuartzCore/QuartzCore.h>
#include "ios_main.h"

// ---------------------------------------------------------------------------
// touch view: CAEAGLLayer host + all-touches capture
// ---------------------------------------------------------------------------
@interface GameTouchView : UIView
@end
@implementation GameTouchView
+ (Class)layerClass { return [CAEAGLLayer class]; }
- (BOOL)canBecomeFirstResponder { return YES; }
// multipleTouches enabled via multipleTouchEnabled
@end

// ---------------------------------------------------------------------------
@interface GameViewController ()
@property (strong) GameTouchView* gameView;
@property (strong) CADisplayLink* displayLink;
@property (nonatomic) CFTimeInterval lastFrame;
@property (strong) NSMutableDictionary<NSValue*, NSNumber*>* touchIds;
@property (nonatomic) int nextTouchId;
@end

@implementation GameViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.72 green:0.89 blue:0.93 alpha:1];
    self.gameView = [[GameTouchView alloc] initWithFrame:self.view.bounds];
    self.gameView.multipleTouchEnabled = YES;
    self.gameView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:self.gameView];
    self.touchIds = [NSMutableDictionary new];
    self.nextTouchId = 0;

    // native init once: Documents dir (saves) + bundle dir (textures)
    NSString* docs = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    NSString* bundle = [NSBundle mainBundle].resourcePath;
    iosMain_init(docs.UTF8String, bundle.UTF8String);

    // pause/resume hooks
    [[NSNotificationCenter defaultCenter] addObserver:self
        selector:@selector(appWillPause) name:UIApplicationWillResignActiveNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
        selector:@selector(appDidResume) name:UIApplicationDidBecomeActiveNotification object:nil];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGRect b = self.gameView.bounds;
    iosMain_resize((__bridge void*)self.gameView.layer, (int)(b.size.width * self.gameView.contentScaleFactor),
                   (int)(b.size.height * self.gameView.contentScaleFactor));
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (!self.displayLink) {
        self.lastFrame = CACurrentMediaTime();
        self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
        self.displayLink.preferredFramesPerSecond = 60;
        [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSDefaultRunLoopMode];
    }
}

- (void)tick:(CADisplayLink*)link {
    CFTimeInterval now = link.timestamp;
    float dt = (float)(now - self.lastFrame);
    self.lastFrame = now;
    (void)dt;   // the game loop uses a fixed 1/60 step, same as the Android port
    iosMain_frame(YES);
}

- (void)appWillPause {
    if (self.displayLink) {
        [self.displayLink invalidate];
        self.displayLink = nil;
    }
    iosMain_pause();
}

- (void)appDidResume {
    iosMain_resume();
    if (!self.displayLink) {
        self.lastFrame = CACurrentMediaTime();
        self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
        self.displayLink.preferredFramesPerSecond = 60;
        [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSDefaultRunLoopMode];
    }
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    if (self.displayLink) [self.displayLink invalidate];
    iosMain_shutdown();
}

// ---------------------------------------------------------------------------
// touch forwarding: map UITouch* -> stable small id (0..7)
// ---------------------------------------------------------------------------
- (int)idForTouch:(UITouch*)t {
    NSValue* key = [NSValue valueWithPointer:(__bridge const void*)t];
    NSNumber* existing = self.touchIds[key];
    if (existing) return existing.intValue;
    if (self.touchIds.count >= 8) return 7;
    int id = self.nextTouchId++ % 8;
    self.touchIds[key] = @(id);
    return id;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event;
    for (UITouch* t in touches) {
        CGPoint p = [t locationInView:self.gameView];
        p.x *= self.gameView.contentScaleFactor;
        p.y *= self.gameView.contentScaleFactor;
        iosMain_touch(0, (uintptr_t)[self idForTouch:t], p.x, p.y);
    }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event;
    for (UITouch* t in touches) {
        CGPoint p = [t locationInView:self.gameView];
        p.x *= self.gameView.contentScaleFactor;
        p.y *= self.gameView.contentScaleFactor;
        iosMain_touch(1, (uintptr_t)[self idForTouch:t], p.x, p.y);
    }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)event;
    for (UITouch* t in touches) {
        CGPoint p = [t locationInView:self.gameView];
        p.x *= self.gameView.contentScaleFactor;
        p.y *= self.gameView.contentScaleFactor;
        iosMain_touch(2, (uintptr_t)[self idForTouch:t], p.x, p.y);
        [self.touchIds removeObjectForKey:[NSValue valueWithPointer:(__bridge const void*)t]];
    }
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self touchesEnded:touches withEvent:event];
}

// rotate with the device: landscape 16:9 like the Android mobile build
// (sensorLandscape) — HUD layout is proportional and adapts to w/h
- (BOOL)shouldAutorotate { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskLandscape;
}

@end
