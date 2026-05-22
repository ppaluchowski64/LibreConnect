#include "MediaTrackInfo.h"
#include <DebugLog.h>

#import <Foundation/Foundation.h>

#include <optional>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

namespace {
    struct CachedState {
        TrackMetadata info;
        long long timestamp = 0;
        long long rawPosMicros = 0;
    };

    NSString* GetHelperPath(NSString* name, NSString* extension) {
        NSString* path = [[NSBundle mainBundle] pathForResource:name ofType:extension];

        if (path)
            return path;

        if ([extension isEqualToString:@"framework"]) {
            path = [[[NSBundle mainBundle] privateFrameworksPath] stringByAppendingPathComponent:[NSString stringWithFormat:@"%@.%@", name, extension]];

            if ([[NSFileManager defaultManager] fileExistsAtPath:path])
                return path;
        }

        NSString* execPath = [[NSBundle mainBundle] executablePath];

        if (execPath) {
            NSString* dir = [execPath stringByDeletingLastPathComponent];
            path = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"%@.%@", name, extension]];

            if ([[NSFileManager defaultManager] fileExistsAtPath:path])
                return path;

            if ([extension isEqualToString:@"framework"]) {
                path = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"Frameworks/%@.%@", name, extension]];

                if ([[NSFileManager defaultManager] fileExistsAtPath:path])
                    return path;
            }
        }

        return nil;
    }

    class MediaWorker {
        public:
            static MediaWorker& Get() {
                static MediaWorker instance;
                return instance;
            }

            std::optional<CachedState> GetRawState() {
                std::unique_lock lock(m_mutex);
                StartIfNeeded();

                return m_state;
            }

        private:
            MediaWorker() = default;

            void StartIfNeeded() {
                if (m_task && [m_task isRunning])
                    return;

                NSString* script = GetHelperPath(@"mediaremote-adapter", @"pl");
                NSString* framework = GetHelperPath(@"MediaRemoteAdapter", @"framework");

                if (!script || !framework)
                    return;

                m_task = [[NSTask alloc] init];
                [m_task setLaunchPath:@"/usr/bin/perl"];
                [m_task setArguments:@[script, framework, @"stream", @"--no-diff", @"--debounce=100", @"--micros"]];

                NSPipe* pipe = [NSPipe pipe];
                [m_task setStandardOutput:pipe];
                [m_task setStandardError:[NSFileHandle fileHandleWithNullDevice]];

                __block NSMutableData* lineBuffer = [[NSMutableData alloc] init];

                [[pipe fileHandleForReading] setReadabilityHandler:^(NSFileHandle* handle) {
                    @autoreleasepool {
                        NSData* data = [handle availableData];

                        if (data.length == 0) {
                            [handle setReadabilityHandler:nil];
                            return;
                        }

                        [lineBuffer appendData:data];

                        const char* bytes = (const char*)lineBuffer.bytes;
                        NSUInteger len = lineBuffer.length;
                        NSUInteger start = 0;

                        for (NSUInteger i = 0; i < len; i++) {
                            if (bytes[i] == '\n') {
                                NSUInteger lineLen = i - start;

                                if (lineLen > 0) {
                                    NSData* lineData = [lineBuffer subdataWithRange:NSMakeRange(start, lineLen)];
                                    NSDictionary* dict = [NSJSONSerialization JSONObjectWithData:lineData options:0 error:nil];

                                    if (dict && dict[@"payload"]) {
                                        NSDictionary* p = dict[@"payload"];
                                        CachedState next{};

                                        if (p[@"title"] && p[@"title"] != [NSNull null])
                                            next.info.title = [p[@"title"] UTF8String];

                                        if (p[@"artist"] && p[@"artist"] != [NSNull null])
                                            next.info.artist = [p[@"artist"] UTF8String];

                                        if (p[@"album"] && p[@"album"] != [NSNull null])
                                            next.info.album = [p[@"album"] UTF8String];

                                        if (p[@"durationMicros"] && p[@"durationMicros"] != [NSNull null])
                                            next.info.duration = [p[@"durationMicros"] doubleValue] / 1e6;

                                        if (p[@"elapsedTimeMicros"] && p[@"elapsedTimeMicros"] != [NSNull null])
                                            next.rawPosMicros = [p[@"elapsedTimeMicros"] longLongValue];

                                        if (p[@"playing"] && p[@"playing"] != [NSNull null])
                                            next.info.playing = [p[@"playing"] boolValue];

                                        if (p[@"timestampEpochMicros"] && p[@"timestampEpochMicros"] != [NSNull null])
                                            next.timestamp = [p[@"timestampEpochMicros"] longLongValue];

                                        if (p[@"artworkData"] && p[@"artworkData"] != [NSNull null]) {
                                            NSData* art = [[NSData alloc] initWithBase64EncodedString:p[@"artworkData"] options:0];

                                            if (art) {
                                                auto artBytes = static_cast<const uint8_t*>([art bytes]);
                                                next.info.cover.assign(artBytes, artBytes + [art length]);
                                            }
                                        }

                                        std::unique_lock lock(this->m_mutex);
                                        this->m_state = next;
                                    }
                                }

                                start = i + 1;
                            }
                        }

                        if (start > 0)
                            [lineBuffer replaceBytesInRange:NSMakeRange(0, start) withBytes:NULL length:0];
                    }
                }];

                [m_task launchAndReturnError:nil];
            }

            std::mutex m_mutex;
            std::optional<CachedState> m_state;
            NSTask* m_task = nil;
    };
}

std::optional<TrackMetadata> MediaTrackInfo::GetCurrentTrack() {
    auto stateOpt = MediaWorker::Get().GetRawState();

    if (!stateOpt)
        return std::nullopt;

    TrackMetadata info = stateOpt->info;
    double rawPos = static_cast<double>(stateOpt->rawPosMicros) / 1000000.0;

    info.position = CalculateInterpolatedPosition(
        rawPos,
        stateOpt->timestamp,
        info.playing
    );

    if (info.duration > 0.0 && info.position > info.duration)
        info.position = info.duration;

    return info;
}

void MediaTrackInfo::SetPosition(double seconds) {
    NSString* script = GetHelperPath(@"mediaremote-adapter", @"pl");
    NSString* framework = GetHelperPath(@"MediaRemoteAdapter", @"framework");

    if (!script || !framework)
        return;

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        @autoreleasepool {
            auto micros = static_cast<long long>(seconds * 1e6);
            NSArray* args = @[script, framework, @"seek", [NSString stringWithFormat:@"%lld", micros]];

            NSTask* task = [[NSTask alloc] init];
            [task setLaunchPath:@"/usr/bin/perl"];
            [task setArguments:args];
            [task setStandardOutput:[NSFileHandle fileHandleWithNullDevice]];
            [task setStandardError:[NSFileHandle fileHandleWithNullDevice]];

            if ([task launchAndReturnError:nil])
                [task waitUntilExit];
        }
    });
}
