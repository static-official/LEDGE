// LEDGE iOS — app entry point. UIApplicationMain boots the AppDelegate →
// GameViewController → CADisplayLink → iosMain_frame loop.
#import <UIKit/UIKit.h>
#import "AppDelegate.h"

int main(int argc, char* argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
