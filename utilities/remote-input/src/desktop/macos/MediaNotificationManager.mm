#include "MediaNotificationManager.h"
#include "InputTypes.h"
#include "MediaTrackInfo.h"

#import <MediaPlayer/MediaPlayer.h>
#import <AppKit/AppKit.h>

#include <mutex>

namespace {
    std::mutex g_mutex;
    bool g_isVisible = false;

    id g_playTarget = nil;
    id g_pauseTarget = nil;
    id g_togglePlayPauseTarget = nil;
    id g_nextTrackTarget = nil;
    id g_previousTrackTarget = nil;
    id g_changePlaybackPositionTarget = nil;

    NSMutableDictionary* g_nowPlayingInfo = nil;
}

void MediaNotificationManager::Show() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_isVisible)
        return;

    g_isVisible = true;

    if (!g_nowPlayingInfo)
        g_nowPlayingInfo = [[NSMutableDictionary alloc] init];

    MPRemoteCommandCenter* commandCenter = [MPRemoteCommandCenter sharedCommandCenter];

    g_playTarget = [commandCenter.playCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        MediaNotificationManager::InvokeAction(MediaSignal::PlayPause);
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    g_pauseTarget = [commandCenter.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        MediaNotificationManager::InvokeAction(MediaSignal::PlayPause);
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    g_togglePlayPauseTarget = [commandCenter.togglePlayPauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        MediaNotificationManager::InvokeAction(MediaSignal::PlayPause);
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    g_nextTrackTarget = [commandCenter.nextTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        MediaNotificationManager::InvokeAction(MediaSignal::NextTrack);
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    g_previousTrackTarget = [commandCenter.previousTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        MediaNotificationManager::InvokeAction(MediaSignal::PreviousTrack);
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    g_changePlaybackPositionTarget = [commandCenter.changePlaybackPositionCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
        MPChangePlaybackPositionCommandEvent* posEvent = (MPChangePlaybackPositionCommandEvent*)event;
        MediaNotificationManager::InvokeSeek(posEvent.positionTime);
        return MPRemoteCommandHandlerStatusSuccess;
    }];

    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = g_nowPlayingInfo;
}

void MediaNotificationManager::Hide() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_isVisible)
        return;

    g_isVisible = false;

    MPRemoteCommandCenter* commandCenter = [MPRemoteCommandCenter sharedCommandCenter];
    [commandCenter.playCommand removeTarget:g_playTarget];
    [commandCenter.pauseCommand removeTarget:g_pauseTarget];
    [commandCenter.togglePlayPauseCommand removeTarget:g_togglePlayPauseTarget];
    [commandCenter.nextTrackCommand removeTarget:g_nextTrackTarget];
    [commandCenter.previousTrackCommand removeTarget:g_previousTrackTarget];
    [commandCenter.changePlaybackPositionCommand removeTarget:g_changePlaybackPositionTarget];

    g_playTarget = nil;
    g_pauseTarget = nil;
    g_togglePlayPauseTarget = nil;
    g_nextTrackTarget = nil;
    g_previousTrackTarget = nil;
    g_changePlaybackPositionTarget = nil;

    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = nil;
    if (@available(macOS 10.12.2, *)) {
        [MPNowPlayingInfoCenter defaultCenter].playbackState = MPNowPlayingPlaybackStateStopped;
    }
}

void MediaNotificationManager::UpdateMetadata(const TrackMetadata& metadata) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_nowPlayingInfo)
        g_nowPlayingInfo = [[NSMutableDictionary alloc] init];

    g_nowPlayingInfo[MPMediaItemPropertyTitle] = [NSString stringWithUTF8String:metadata.title.c_str()];
    g_nowPlayingInfo[MPMediaItemPropertyArtist] = [NSString stringWithUTF8String:metadata.artist.c_str()];
    g_nowPlayingInfo[MPMediaItemPropertyAlbumTitle] = [NSString stringWithUTF8String:metadata.album.c_str()];
    g_nowPlayingInfo[MPMediaItemPropertyPlaybackDuration] = @(metadata.duration);

    if (!metadata.cover.empty()) {
        NSData* coverData = [NSData dataWithBytes:metadata.cover.data() length:metadata.cover.size()];
        NSImage* image = [[NSImage alloc] initWithData:coverData];

        if (image) {
            MPMediaItemArtwork* artwork = [[MPMediaItemArtwork alloc] initWithBoundsSize:image.size requestHandler:^NSImage* (CGSize size) {
                return image;
            }];

            g_nowPlayingInfo[MPMediaItemPropertyArtwork] = artwork;
        } else {
            [g_nowPlayingInfo removeObjectForKey:MPMediaItemPropertyArtwork];
        }
    } else {
        [g_nowPlayingInfo removeObjectForKey:MPMediaItemPropertyArtwork];
    }

    if (g_isVisible)
        [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = g_nowPlayingInfo;
}

void MediaNotificationManager::UpdatePlaybackState(bool isPlaying, double position) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_nowPlayingInfo)
        g_nowPlayingInfo = [[NSMutableDictionary alloc] init];

    g_nowPlayingInfo[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(position);
    g_nowPlayingInfo[MPNowPlayingInfoPropertyPlaybackRate] = isPlaying ? @1.0 : @0.0;

    if (g_isVisible) {
        [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = g_nowPlayingInfo;
        if (@available(macOS 10.12.2, *)) {
            [MPNowPlayingInfoCenter defaultCenter].playbackState = isPlaying ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
        }
    }
}

bool MediaNotificationManager::IsVisible() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_isVisible;
}
