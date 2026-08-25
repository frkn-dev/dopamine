#import <UIKit/UIKit.h>
#import <CoreMotion/CoreMotion.h>
#import <objc/runtime.h>
#include <dispatch/dispatch.h>
#include <math.h>

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QString>

#include "ios_controller.h"

using SceneOpenURLContexts = void (*)(id, SEL, UIScene *, NSSet<UIOpenURLContext *> *);

static SceneOpenURLContexts g_originalSceneOpenURLContexts = nullptr;

static void amnezia_handleURL(NSURL *url)
{
    if (!url) {
        return;
    }

    // Handle frkn:// URL scheme
    if ([[url scheme] isEqualToString:@"frkn"]) {
        NSString *urlStr = [url absoluteString];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            // frkn://sub/ and frkn://conn/ are subscription/share links — ImportController
            // handles them natively. Other frkn:// URLs are aliases for https://.
            if ([urlStr hasPrefix:@"frkn://sub/"] || [urlStr hasPrefix:@"frkn://conn/"]) {
                IosController::Instance()->importConfigFromOutside(QString::fromNSString(urlStr));
            } else {
                NSString *httpsUrl = [urlStr stringByReplacingCharactersInRange:NSMakeRange(0, 7) withString:@"https://"];
                IosController::Instance()->importConfigFromOutside(QString::fromNSString(httpsUrl));
            }
        });
        return;
    }

    if (!url.isFileURL) {
        return;
    }

    QString filePath(url.path.UTF8String);
    if (filePath.isEmpty()) {
        return;
    }

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (filePath.contains("backup")) {
            IosController::Instance()->importBackupFromOutside(filePath);
            return;
        }

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }

        const QByteArray data = file.readAll();
        IosController::Instance()->importConfigFromOutside(QString::fromUtf8(data));
    });
}

static void amnezia_scene_openURLContexts(id self, SEL _cmd, UIScene *scene, NSSet<UIOpenURLContext *> *contexts)
{
    if (g_originalSceneOpenURLContexts) {
        g_originalSceneOpenURLContexts(self, _cmd, scene, contexts);
    }

    if (!contexts || contexts.count == 0) {
        return;
    }

    if (@available(iOS 13.0, *)) {
        for (UIOpenURLContext *context in contexts) {
            amnezia_handleURL(context.URL);
        }
    }
}

@interface AmneziaSceneDelegateHooks : NSObject
@end

// Shake detection via CoreMotion (accelerometer) — no runtime swizzling:
// spike in acceleration magnitude => shake, throttled to once per 2 seconds.
static CMMotionManager *g_shakeMotionManager = nil;
static CFAbsoluteTime g_lastShakeTime = 0;

static void amnezia_startShakeDetection(void)
{
    if (g_shakeMotionManager) {
        return;
    }
    CMMotionManager *manager = [CMMotionManager new];
    if (!manager.isAccelerometerAvailable) {
        return;
    }
    g_shakeMotionManager = manager;
    manager.accelerometerUpdateInterval = 0.05;
    [manager startAccelerometerUpdatesToQueue:[NSOperationQueue mainQueue]
                                  withHandler:^(CMAccelerometerData *data, NSError *error) {
        if (!data) {
            return;
        }
        const double gx = data.acceleration.x;
        const double gy = data.acceleration.y;
        const double gz = data.acceleration.z;
        const double magnitude = sqrt(gx * gx + gy * gy + gz * gz);
        const CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
        if (magnitude > 2.7 && now - g_lastShakeTime > 2.0) {
            g_lastShakeTime = now;
            IosController::Instance()->notifyShakeDetected();
        }
    }];
}

@implementation AmneziaSceneDelegateHooks

+ (void)load
{
    // start shake detection shortly after launch (same deferral pattern as the URL hook)
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        amnezia_startShakeDetection();
    });

    Class cls = objc_getClass("QIOSWindowSceneDelegate");
    if (!cls) {
        return;
    }

    SEL selector = @selector(scene:openURLContexts:);
    Method method = class_getInstanceMethod(cls, selector);
    if (method) {
        g_originalSceneOpenURLContexts = reinterpret_cast<SceneOpenURLContexts>(method_getImplementation(method));
        method_setImplementation(method, reinterpret_cast<IMP>(amnezia_scene_openURLContexts));
    } else {
        const char *types = "v@:@@";
        class_addMethod(cls, selector, reinterpret_cast<IMP>(amnezia_scene_openURLContexts), types);
    }
}

@end
