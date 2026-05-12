#include "PlatformVirtualFileDrag.h"

#include <QGuiApplication>
#include <QPointer>
#include <QWindow>

#import <AppKit/AppKit.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct PromiseDragState {
    explicit PromiseDragState(PlatformVirtualFileDrag::PromiseCompletionFn completionFn, const int promiseCount)
        : completion(std::move(completionFn))
        , totalPromises(promiseCount)
    {
    }

    void MarkWriteStarted()
    {
        std::lock_guard lock(mutex);
        ++writesStarted;
    }

    void MarkWriteFinished(const bool success, const std::filesystem::path& cleanupPath)
    {
        PlatformVirtualFileDrag::PromiseCompletionFn completionToCall;
        std::function<void()> finishedObserverToCall;
        std::vector<std::filesystem::path> cleanupPathsToReturn;
        bool finalSuccess = false;

        {
            std::lock_guard lock(mutex);
            if (!success) {
                allWritesSucceeded = false;
            }

            if (!cleanupPath.empty()) {
                cleanupPaths.push_back(cleanupPath);
            }

            ++completedWrites;
            if (!finished && completedWrites >= totalPromises) {
                finished = true;
                finalSuccess = allWritesSucceeded;
                cleanupPathsToReturn = cleanupPaths;
                completionToCall = completion;
                finishedObserverToCall = finishedObserver;
            }
        }

        if (completionToCall) {
            completionToCall(finalSuccess, std::move(cleanupPathsToReturn));
        }
        if (finishedObserverToCall) {
            finishedObserverToCall();
        }
    }

    void MarkDragCancelledIfUnused()
    {
        PlatformVirtualFileDrag::PromiseCompletionFn completionToCall;
        std::function<void()> finishedObserverToCall;

        {
            std::lock_guard lock(mutex);
            if (finished || writesStarted > 0) {
                return;
            }

            finished = true;
            completionToCall = completion;
            finishedObserverToCall = finishedObserver;
        }

        if (completionToCall) {
            completionToCall(false, {});
        }
        if (finishedObserverToCall) {
            finishedObserverToCall();
        }
    }

    std::mutex mutex;
    PlatformVirtualFileDrag::PromiseCompletionFn completion;
    std::function<void()> finishedObserver;
    std::vector<std::filesystem::path> cleanupPaths;
    int totalPromises = 0;
    int writesStarted = 0;
    int completedWrites = 0;
    bool allWritesSucceeded = true;
    bool finished = false;
};

NSString* NSStringFromStdString(const std::string& value)
{
    return [NSString stringWithUTF8String:value.c_str()] ?: @"";
}

NSError* NSErrorFromMessage(NSString* message)
{
    return [NSError errorWithDomain:@"LibreConnect.FilePromiseDrag"
                               code:1
                           userInfo:@{NSLocalizedDescriptionKey: message}];
}

NSImage* DragImageForFileName(NSString* fileName)
{
    NSString* extension = [fileName pathExtension];
    NSImage* image = [[NSWorkspace sharedWorkspace] iconForFileType:extension.length > 0 ? extension : @"data"];
    image.size = NSMakeSize(32.0, 32.0);
    return image;
}
}

@interface LCFilePromiseDelegate : NSObject <NSFilePromiseProviderDelegate>
- (instancetype)initWithFileName:(NSString*)fileName
                         resolver:(PlatformVirtualFileDrag::ResolvePathFn)resolver
                            state:(std::shared_ptr<PromiseDragState>)state;
@end

@implementation LCFilePromiseDelegate {
    NSString* _fileName;
    PlatformVirtualFileDrag::ResolvePathFn _resolver;
    std::shared_ptr<PromiseDragState> _state;
}

- (instancetype)initWithFileName:(NSString*)fileName
                         resolver:(PlatformVirtualFileDrag::ResolvePathFn)resolver
                            state:(std::shared_ptr<PromiseDragState>)state
{
    self = [super init];
    if (self) {
        _fileName = [fileName copy];
        _resolver = std::move(resolver);
        _state = std::move(state);
    }
    return self;
}

- (void)dealloc
{
    [_fileName release];
    [super dealloc];
}

- (NSString*)filePromiseProvider:(NSFilePromiseProvider*)filePromiseProvider fileNameForType:(NSString*)fileType
{
    Q_UNUSED(filePromiseProvider);
    Q_UNUSED(fileType);
    return _fileName;
}

- (NSOperationQueue*)operationQueueForFilePromiseProvider:(NSFilePromiseProvider*)filePromiseProvider
{
    Q_UNUSED(filePromiseProvider);
    static NSOperationQueue* queue = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        queue = [[NSOperationQueue alloc] init];
        queue.name = @"LibreConnect file promise writer";
        queue.maxConcurrentOperationCount = 1;
    });
    return queue;
}

- (void)filePromiseProvider:(NSFilePromiseProvider*)filePromiseProvider
          writePromiseToURL:(NSURL*)url
          completionHandler:(void (^)(NSError* _Nullable))completionHandler
{
    Q_UNUSED(filePromiseProvider);
    if (_state) {
        _state->MarkWriteStarted();
    }

    std::filesystem::path preparedPath;
    if (_resolver) {
        preparedPath = _resolver();
    }

    NSError* error = nil;
    bool success = false;
    if (preparedPath.empty() || !std::filesystem::exists(preparedPath)) {
        error = NSErrorFromMessage(@"LibreConnect could not prepare the promised file.");
    } else {
        NSURL* sourceURL = [NSURL fileURLWithPath:NSStringFromStdString(preparedPath.string())];
        success = [[NSFileManager defaultManager] copyItemAtURL:sourceURL toURL:url error:&error];
    }

    if (_state) {
        _state->MarkWriteFinished(success, preparedPath);
    }

    completionHandler(success ? nil : error);
}

@end

@interface LCFilePromiseDragSource : NSObject <NSDraggingSource>
- (instancetype)initWithState:(std::shared_ptr<PromiseDragState>)state delegates:(NSArray*)delegates;
@end

@implementation LCFilePromiseDragSource {
    std::shared_ptr<PromiseDragState> _state;
    NSArray* _delegates;
}

+ (NSMutableSet*)activeSources
{
    static NSMutableSet* sources = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sources = [[NSMutableSet alloc] init];
    });
    return sources;
}

- (instancetype)initWithState:(std::shared_ptr<PromiseDragState>)state delegates:(NSArray*)delegates
{
    self = [super init];
    if (self) {
        _state = std::move(state);
        _delegates = [delegates copy];
        LCFilePromiseDragSource* source = self;
        _state->finishedObserver = [source]() {
            dispatch_async(dispatch_get_main_queue(), ^{
                [[LCFilePromiseDragSource activeSources] removeObject:source];
            });
        };
        [[[self class] activeSources] addObject:self];
    }
    return self;
}

- (void)dealloc
{
    [_delegates release];
    [super dealloc];
}

- (NSDragOperation)draggingSession:(NSDraggingSession*)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
    Q_UNUSED(session);
    Q_UNUSED(context);
    return NSDragOperationCopy;
}

- (BOOL)ignoreModifierKeysForDraggingSession:(NSDraggingSession*)session
{
    Q_UNUSED(session);
    return YES;
}

- (void)draggingSession:(NSDraggingSession*)session endedAtPoint:(NSPoint)screenPoint operation:(NSDragOperation)operation
{
    Q_UNUSED(session);
    Q_UNUSED(screenPoint);
    if (operation == NSDragOperationNone && _state) {
        _state->MarkDragCancelledIfUnused();
    }
}

@end

bool PlatformVirtualFileDrag::Start(QObject* dragSource, ResolvePathsFn resolver)
{
    Q_UNUSED(dragSource);
    Q_UNUSED(resolver);
    return false;
}

bool PlatformVirtualFileDrag::StartPromisedFiles(QObject* dragSource, std::vector<PromisedFile> files, PromiseCompletionFn completion)
{
    Q_UNUSED(dragSource);
    if (files.empty()) {
        return false;
    }

    QWindow* window = QGuiApplication::focusWindow();
    if (!window) {
        return false;
    }

    NSView* sourceView = reinterpret_cast<NSView*>(window->winId());
    if (!sourceView) {
        return false;
    }

    NSEvent* event = [NSApp currentEvent];
    if (!event) {
        return false;
    }

    auto state = std::make_shared<PromiseDragState>(std::move(completion), static_cast<int>(files.size()));
    NSMutableArray<NSDraggingItem*>* draggingItems = [NSMutableArray arrayWithCapacity:files.size()];
    NSMutableArray* delegates = [NSMutableArray arrayWithCapacity:files.size()];

    NSPoint cursorPoint = [sourceView convertPoint:event.locationInWindow fromView:nil];
    for (PlatformVirtualFileDrag::PromisedFile& file : files) {
        NSString* fileName = NSStringFromStdString(file.fileName);
        if (fileName.length == 0 || !file.resolver) {
            continue;
        }

        LCFilePromiseDelegate* delegate = [[LCFilePromiseDelegate alloc] initWithFileName:fileName
                                                                                 resolver:std::move(file.resolver)
                                                                                    state:state];
        [delegates addObject:delegate];
        NSFilePromiseProvider* provider = [[NSFilePromiseProvider alloc] initWithFileType:@"public.data" delegate:delegate];
        NSDraggingItem* draggingItem = [[NSDraggingItem alloc] initWithPasteboardWriter:provider];
        NSRect frame = NSMakeRect(cursorPoint.x - 16.0, cursorPoint.y - 16.0, 32.0, 32.0);
        [draggingItem setDraggingFrame:frame contents:DragImageForFileName(fileName)];
        [draggingItems addObject:draggingItem];
        [draggingItem release];
        [provider release];
        [delegate release];
    }

    if (draggingItems.count == 0) {
        return false;
    }

    LCFilePromiseDragSource* source = [[LCFilePromiseDragSource alloc] initWithState:state delegates:delegates];
    NSDraggingSession* session = [sourceView beginDraggingSessionWithItems:draggingItems event:event source:source];
    [source release];
    session.animatesToStartingPositionsOnCancelOrFail = YES;
    return true;
}
