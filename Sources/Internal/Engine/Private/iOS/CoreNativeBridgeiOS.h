#pragma once

#include "Base/BaseTypes.h"

#if defined(__DAVAENGINE_COREV2__)
#if defined(__DAVAENGINE_IPHONE__)

#include "Concurrency/Mutex.h"
#include "Engine/Private/EnginePrivateFwd.h"

@class NSObject;
@class NSDictionary;
@class UIApplication;
@class ObjectiveCInterop;
@class NotificationBridge;
@class UILocalNotification;

@protocol DVEApplicationListener;

namespace DAVA
{
namespace Private
{
// Bridge between C++ and Objective-C for iOS's PlatformCore class
// Responsibilities:
//  - holds neccesary Objective-C objects
//
// CoreNativeBridge is friend of iOS's PlatformCore
struct CoreNativeBridge final
{
    CoreNativeBridge(PlatformCore* core);
    ~CoreNativeBridge();

    void Run();
    void OnFrameTimer();

    // Callbacks from AppDelegateiOS
    bool ApplicationWillFinishLaunchingWithOptions(UIApplication* app, NSDictionary* launchOptions);
    bool ApplicationDidFinishLaunchingWithOptions(UIApplication* app, NSDictionary* launchOptions);
    void ApplicationDidBecomeActive(UIApplication* app);
    void ApplicationWillResignActive(UIApplication* app);
    void ApplicationDidEnterBackground(UIApplication* app);
    void ApplicationWillEnterForeground(UIApplication* app);
    void ApplicationWillTerminate(UIApplication* app);
    void ApplicationDidReceiveMemoryWarning(UIApplication* app);
    void ApplicationDidReceiveLocalNotification(UIApplication* app, UILocalNotification* notification);
    void DidReceiveRemoteNotification(UIApplication* app, NSDictionary* userInfo);
    void DidRegisterForRemoteNotificationsWithDeviceToken(UIApplication* app, NSData* deviceToken);
    void DidFailToRegisterForRemoteNotificationsWithError(UIApplication* app, NSError* error);
    void DidReceiveLocalNotification(UIApplication* app, UILocalNotification* notification);
    void HandleActionWithIdentifier(UIApplication* app, NSString* identifier, NSDictionary* userInfo, id completionHandler);
    bool OpenURL(UIApplication* app, NSURL* url, NSString* sourceApplication, id annotation);

    void GameControllerDidConnected();
    void GameControllerDidDisconnected();

    void RegisterDVEApplicationListener(id<DVEApplicationListener> listener);
    void UnregisterDVEApplicationListener(id<DVEApplicationListener> listener);

    enum eNotificationType
    {
        ON_DID_FINISH_LAUNCHING,
        ON_WILL_FINISH_LAUNCHING,
        ON_DID_BECOME_ACTIVE,
        ON_WILL_RESIGN_ACTIVE,
        ON_DID_ENTER_BACKGROUND,
        ON_WILL_ENTER_FOREGROUND,
        ON_WILL_TERMINATE,
        ON_DID_RECEIVE_MEMORY_WARNING,
        ON_DID_REGISTER_FOR_REMOTE_NOTIFICATION_WITH_TOKEN,
        ON_DID_FAIL_REGISTER_FOR_REMOTE_NOTIFICATION_WITH_ERROR,
        ON_DID_RECEIVE_REMOTE_NOTIFICATION,
        ON_DID_RECEIVE_LOCAL_NOTIFICATION,
        ON_HANDLE_ACTION_WITH_IDENTIFIER,
        ON_OPEN_URL
    };

    bool NotifyListeners(eNotificationType type, NSObject* arg1 = nullptr, NSObject* arg2 = nullptr, NSObject* arg3 = nullptr, id arg4 = nullptr);

    PlatformCore* core = nullptr;
    EngineBackend* engineBackend = nullptr;
    MainDispatcher* mainDispatcher = nullptr;
    ObjectiveCInterop* objcInterop = nullptr;

    Mutex listenersMutex;
    NSMutableArray* appDelegateListeners;
};

} // namespace Private
} // namespace DAVA

#endif // __DAVAENGINE_IPHONE__
#endif // __DAVAENGINE_COREV2__
