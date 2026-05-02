#include "MediaTrackInfo.h"
#include <DebugLog.h>

#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <dispatch/dispatch.h>

#include <optional>
#include <string>
#include <vector>

namespace {
    void* GetMediaRemoteHandle() {
        static void* handle = dlopen("/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote", RTLD_LAZY);
        return handle;
    }

    void* GetMediaRemoteFunction(const char* symbolName) {
        void* handle = GetMediaRemoteHandle();

        if (!handle)
            return nullptr;

        return dlsym(handle, symbolName);
    }

    NSString* GetMediaRemoteStringConstant(const char* symbolName) {
        void* handle = GetMediaRemoteHandle();

        if (!handle)
            return nil;

        CFStringRef* strRefPtr = (CFStringRef*)dlsym(handle, symbolName);

        if (strRefPtr && *strRefPtr)
            return (__bridge NSString*)(*strRefPtr);

        return nil;
    }
}

std::optional<TrackMetadata> MediaTrackInfo::GetCurrentTrack() {
    typedef void (^MRInfoBlock)(CFDictionaryRef);
    typedef void (*MRGetInfoFunc)(dispatch_queue_t, MRInfoBlock);

    auto MRMediaRemoteGetNowPlayingInfo = (MRGetInfoFunc)GetMediaRemoteFunction("MRMediaRemoteGetNowPlayingInfo");

    if (!MRMediaRemoteGetNowPlayingInfo)
        return std::nullopt;

    static NSString* kTitle = GetMediaRemoteStringConstant("kMRMediaRemoteNowPlayingInfoTitle") ?: @"kMRMediaRemoteNowPlayingInfoTitle";
    static NSString* kArtist = GetMediaRemoteStringConstant("kMRMediaRemoteNowPlayingInfoArtist") ?: @"kMRMediaRemoteNowPlayingInfoArtist";
    static NSString* kAlbum = GetMediaRemoteStringConstant("kMRMediaRemoteNowPlayingInfoAlbum") ?: @"kMRMediaRemoteNowPlayingInfoAlbum";
    static NSString* kDuration = GetMediaRemoteStringConstant("kMRMediaRemoteNowPlayingInfoDuration") ?: @"kMRMediaRemoteNowPlayingInfoDuration";
    static NSString* kPosition = GetMediaRemoteStringConstant("kMRMediaRemoteNowPlayingInfoElapsedTime") ?: @"kMRMediaRemoteNowPlayingInfoElapsedTime";
    static NSString* kRate = GetMediaRemoteStringConstant("kMRMediaRemoteNowPlayingInfoPlaybackRate") ?: @"kMRMediaRemoteNowPlayingInfoPlaybackRate";
    static NSString* kArtwork = GetMediaRemoteStringConstant("kMRMediaRemoteNowPlayingInfoArtworkData") ?: @"kMRMediaRemoteNowPlayingInfoArtworkData";

    __block TrackMetadata info{};
    __block bool found = false;

    if ([NSThread isMainThread])
        Debug::LogWarning("MediaTrackInfo::GetCurrentTrack() called on main thread! Potential deadlock.");

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    MRMediaRemoteGetNowPlayingInfo(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(CFDictionaryRef information) {
        if (information) {
            NSDictionary *dict = (__bridge NSDictionary *)information;

            NSString *title = dict[kTitle];
            NSString *artist = dict[kArtist];
            NSString *album = dict[kAlbum];
            NSNumber *duration = dict[kDuration];
            NSNumber *position = dict[kPosition];
            NSNumber *rate = dict[kRate];
            NSData *artwork = dict[kArtwork];

            if (title) info.title = [title UTF8String];
            if (artist) info.artist = [artist UTF8String];
            if (album) info.album = [album UTF8String];
            if (duration) info.duration = [duration doubleValue];
            if (position) info.position = [position doubleValue];

            if (rate) info.playing = ([rate doubleValue] > 0.0);

            if (artwork) {
                const uint8_t* bytes = (const uint8_t*)[artwork bytes];
                info.cover.assign(bytes, bytes + [artwork length]);
            }

            found = true;
        }

        dispatch_semaphore_signal(semaphore);
    });

    dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)));

    if (found)
        return info;

    return std::nullopt;
}

void MediaTrackInfo::SetPosition(double seconds) {
    const uint32_t MRMediaRemoteCommandSeekToPlaybackPosition = 45;

    typedef void (*MRSendCommandFunc)(uint32_t, CFDictionaryRef, void*);
    auto MRMediaRemoteSendCommand = (MRSendCommandFunc)GetMediaRemoteFunction("MRMediaRemoteSendCommand");

    if (!MRMediaRemoteSendCommand)
        return;

    static NSString* kOptionPosition = GetMediaRemoteStringConstant("kMRMediaRemoteOptionPlaybackPosition") ?: @"kMRMediaRemoteOptionPlaybackPosition";

    NSDictionary *options = @{
        kOptionPosition : @(seconds)
    };

    MRMediaRemoteSendCommand(MRMediaRemoteCommandSeekToPlaybackPosition, (__bridge CFDictionaryRef)options, nil);
}
