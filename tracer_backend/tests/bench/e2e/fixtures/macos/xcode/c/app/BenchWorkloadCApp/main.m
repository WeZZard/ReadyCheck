#import <Cocoa/Cocoa.h>
#include "workload.h"

@interface BenchAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation BenchAppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    bench_workload_run();
    [NSApp terminate:nil];
}
@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        BenchAppDelegate *delegate = [[BenchAppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
