#include <NotificationEmitter.h>

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include <cstdlib>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

std::mutex g_actionsMutex;
std::unordered_map<int64_t, std::vector<std::function<void()>>> g_actionsByNotificationId;
std::atomic<int64_t> g_nextNotificationId{1};
std::once_flag g_delegateOnce;
std::once_flag g_authorizationOnce;
id<UNUserNotificationCenterDelegate> g_delegate = nil;

constexpr const char* kRequestPrefix = "LibreConnect_";
constexpr const char* kActionPrefix = "BTN_";

bool IsBundledMacAppProcess() {
    NSBundle* bundle = [NSBundle mainBundle];
    if (bundle == nil) {
        return false;
    }

    NSString* bundleIdentifier = [bundle bundleIdentifier];
    if (bundleIdentifier == nil || [bundleIdentifier length] == 0) {
        return false;
    }

    NSString* bundlePath = [[bundle bundleURL] path];
    if (bundlePath == nil || ![[bundlePath pathExtension] isEqualToString:@"app"]) {
        return false;
    }

    return true;
}

NSString* ToNSString(const std::wstring& value) {
    if (value.empty()) {
        return @"";
    }

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    constexpr NSStringEncoding kWideEncoding = NSUTF32LittleEndianStringEncoding;
#else
    constexpr NSStringEncoding kWideEncoding = NSUTF32BigEndianStringEncoding;
#endif

    NSString* converted = [[NSString alloc] initWithBytes:value.data()
                                                   length:value.size() * sizeof(wchar_t)
                                                 encoding:kWideEncoding];
    return converted ?: @"";
}

bool ParseNotificationId(NSString* requestIdentifier, int64_t& id) {
    if (requestIdentifier == nil) {
        return false;
    }

    const std::string identifier = [requestIdentifier UTF8String] ? [requestIdentifier UTF8String] : "";
    if (!identifier.starts_with(kRequestPrefix)) {
        return false;
    }

    const char* numericPart = identifier.c_str() + std::strlen(kRequestPrefix);
    char* endPtr = nullptr;
    const long long parsed = std::strtoll(numericPart, &endPtr, 10);
    if (endPtr == numericPart || *endPtr != '\0') {
        return false;
    }

    id = static_cast<int64_t>(parsed);
    return true;
}

bool ParseActionIndex(NSString* actionIdentifier, size_t& index) {
    if (actionIdentifier == nil) {
        return false;
    }

    const std::string action = [actionIdentifier UTF8String] ? [actionIdentifier UTF8String] : "";
    if (!action.starts_with(kActionPrefix)) {
        return false;
    }

    const char* numericPart = action.c_str() + std::strlen(kActionPrefix);
    char* endPtr = nullptr;
    const long parsed = std::strtol(numericPart, &endPtr, 10);
    if (endPtr == numericPart || *endPtr != '\0' || parsed < 0) {
        return false;
    }

    index = static_cast<size_t>(parsed);
    return true;
}

void PostAsync(std::function<void()> action) {
    if (!action) {
        return;
    }

    std::thread([action = std::move(action)]() mutable { action(); }).detach();
}

} // namespace

@interface LibreConnectNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation LibreConnectNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
didReceiveNotificationResponse:(UNNotificationResponse*)response
         withCompletionHandler:(void (^)(void))completionHandler {
    (void)center;

    int64_t notificationId = 0;
    size_t actionIndex = 0;
    if (!ParseNotificationId(response.notification.request.identifier, notificationId) ||
        !ParseActionIndex(response.actionIdentifier, actionIndex)) {
        if (completionHandler) {
            completionHandler();
        }
        return;
    }

    std::function<void()> action;
    {
        std::lock_guard lock(g_actionsMutex);
        auto it = g_actionsByNotificationId.find(notificationId);
        if (it != g_actionsByNotificationId.end() && actionIndex < it->second.size()) {
            action = it->second[actionIndex];
        }
    }

    PostAsync(std::move(action));

    if (completionHandler) {
        completionHandler();
    }
}
@end

namespace {

void EnsureMacNotificationSetup() {
    std::call_once(g_delegateOnce, []() {
        g_delegate = [LibreConnectNotificationDelegate new];
        [UNUserNotificationCenter currentNotificationCenter].delegate = g_delegate;
    });

    std::call_once(g_authorizationOnce, []() {
        UNAuthorizationOptions options = UNAuthorizationOptionAlert | UNAuthorizationOptionSound | UNAuthorizationOptionBadge;
        [[UNUserNotificationCenter currentNotificationCenter]
            requestAuthorizationWithOptions:options
                          completionHandler:^(__unused BOOL granted, __unused NSError* error) {
                          }];
    });
}

} // namespace

int64_t NotificationEmitter::Emit(
    const std::wstring& notificationName,
    const std::wstring& notificationContent,
    const std::optional<std::filesystem::path>& appIconPath,
    const std::optional<std::filesystem::path>& mainImagePath,
    const std::vector<ButtonAction>& buttons) {
    (void)appIconPath;
    (void)mainImagePath;

    if (!IsBundledMacAppProcess()) {
        return -1;
    }

    EnsureMacNotificationSetup();

    const int64_t notificationId = g_nextNotificationId.fetch_add(1);
    NSString* requestIdentifier = [NSString stringWithFormat:@"%s%lld", kRequestPrefix, notificationId];

    UNMutableNotificationContent* content = [UNMutableNotificationContent new];
    content.title = ToNSString(notificationName);
    content.body = ToNSString(notificationContent);

    if (!buttons.empty()) {
        NSMutableArray<UNNotificationAction*>* actions = [NSMutableArray arrayWithCapacity:buttons.size()];
        for (size_t i = 0; i < buttons.size(); ++i) {
            NSString* actionIdentifier = [NSString stringWithFormat:@"%s%zu", kActionPrefix, i];
            UNNotificationAction* action = [UNNotificationAction actionWithIdentifier:actionIdentifier
                                                                                 title:ToNSString(buttons[i].text)
                                                                               options:UNNotificationActionOptionNone];
            [actions addObject:action];
        }

        NSString* categoryIdentifier = [NSString stringWithFormat:@"LibreConnect_category_%lld", notificationId];
        content.categoryIdentifier = categoryIdentifier;

        UNNotificationCategory* category = [UNNotificationCategory categoryWithIdentifier:categoryIdentifier
                                                                                   actions:actions
                                                                         intentIdentifiers:@[]
                                                                                   options:UNNotificationCategoryOptionCustomDismissAction];

        UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
        [center getNotificationCategoriesWithCompletionHandler:^(NSSet<UNNotificationCategory*>* existingCategories) {
            NSMutableSet<UNNotificationCategory*>* categories = existingCategories
                ? [existingCategories mutableCopy]
                : [NSMutableSet set];
            [categories addObject:category];
            [center setNotificationCategories:categories];
        }];

        std::vector<std::function<void()>> actionsToStore;
        actionsToStore.reserve(buttons.size());
        for (auto& button : buttons) {
            actionsToStore.push_back(button.action);
        }

        std::lock_guard lock(g_actionsMutex);
        g_actionsByNotificationId[notificationId] = std::move(actionsToStore);
    }

    UNTimeIntervalNotificationTrigger* trigger = [UNTimeIntervalNotificationTrigger triggerWithTimeInterval:0.1 repeats:NO];
    UNNotificationRequest* request = [UNNotificationRequest requestWithIdentifier:requestIdentifier content:content trigger:trigger];

    [[UNUserNotificationCenter currentNotificationCenter] addNotificationRequest:request
                                                            withCompletionHandler:^(__unused NSError* error) {
                                                            }];

    return notificationId;
}

void NotificationEmitter::Remove(const int64_t id) {
    if (id < 0 || !IsBundledMacAppProcess()) {
        return;
    }

    NSString* requestIdentifier = [NSString stringWithFormat:@"%s%lld", kRequestPrefix, id];
    NSArray<NSString*>* identifiers = @[ requestIdentifier ];

    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    [center removePendingNotificationRequestsWithIdentifiers:identifiers];
    [center removeDeliveredNotificationsWithIdentifiers:identifiers];

    std::lock_guard lock(g_actionsMutex);
    g_actionsByNotificationId.erase(id);
}
