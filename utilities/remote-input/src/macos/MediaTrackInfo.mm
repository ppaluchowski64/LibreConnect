#include "MediaTrackInfo.h"
#include <DebugLog.h>

#import <Foundation/Foundation.h>

#include <optional>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

namespace {
    struct ExtractedData {
        TrackMetadata info;
        long long timestampEpochMicros = 0;
        long long elapsedTimeMicros = 0;
        double playbackRate = 0.0;
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

    std::optional<ExtractedData> ParseMetadata(NSDictionary* j) {
        if (![j isKindOfClass:[NSDictionary class]])
            return std::nullopt;

        ExtractedData data{};

        if (j[@"title"] && j[@"title"] != [NSNull null])
            data.info.title = [j[@"title"] UTF8String];

        if (j[@"artist"] && j[@"artist"] != [NSNull null])
            data.info.artist = [j[@"artist"] UTF8String];

        if (j[@"album"] && j[@"album"] != [NSNull null])
            data.info.album = [j[@"album"] UTF8String];

        if (j[@"durationMicros"] && j[@"durationMicros"] != [NSNull null])
            data.info.duration = [j[@"durationMicros"] doubleValue] / 1e6;

        if (j[@"elapsedTimeMicros"] && j[@"elapsedTimeMicros"] != [NSNull null]) {
            data.elapsedTimeMicros = [j[@"elapsedTimeMicros"] longLongValue];
            data.info.position = static_cast<double>(data.elapsedTimeMicros) / 1e6;
        }

        if (j[@"playing"] && j[@"playing"] != [NSNull null])
            data.info.playing = [j[@"playing"] boolValue];

        if (j[@"timestampEpochMicros"] && j[@"timestampEpochMicros"] != [NSNull null])
            data.timestampEpochMicros = [j[@"timestampEpochMicros"] longLongValue];

        if (j[@"playbackRate"] && j[@"playbackRate"] != [NSNull null])
            data.playbackRate = [j[@"playbackRate"] doubleValue];

        if (j[@"artworkData"] && j[@"artworkData"] != [NSNull null]) {
            NSData* art = [[NSData alloc] initWithBase64EncodedString:j[@"artworkData"] options:0];

            if (art) {
                auto bytes = static_cast<const uint8_t*>([art bytes]);
                data.info.cover.assign(bytes, bytes + [art length]);
            }
        }

        return data;
    }

    class MediaWorker {
        public:
            static MediaWorker& Get() {
                static MediaWorker instance;
                return instance;
            }

            std::optional<TrackMetadata> GetState() {
                std::unique_lock lock(m_mutex);
                StartIfNeeded();

                if (!m_state)
                    return std::nullopt;

                TrackMetadata info = m_state->info;

                if (info.playing && m_state->playbackRate > 0.0) {
                    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                    ).count();

                    long long diff = now - m_state->timestampEpochMicros;
                    info.position = static_cast<double>(m_state->elapsedTimeMicros + (diff * m_state->playbackRate)) / 1e6;
                }

                if (info.duration > 0.0 && info.position > info.duration)
                    info.position = info.duration;

                return info;
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
                                    auto newState = ParseMetadata(dict[@"payload"]);
                                    std::unique_lock lock(this->m_mutex);
                                    this->m_state = newState;
                                }
                            }

                            start = i + 1;
                        }
                    }

                    if (start > 0) {
                        [lineBuffer replaceBytesInRange:NSMakeRange(0, start) withBytes:NULL length:0];
                    }
                }];

                [m_task launchAndReturnError:nil];
            }

            std::mutex m_mutex;
            std::optional<ExtractedData> m_state;
            NSTask* m_task = nil;
    };
}

std::optional<TrackMetadata> MediaTrackInfo::GetCurrentTrack() {
    return MediaWorker::Get().GetState();
}

void MediaTrackInfo::SetPosition(double seconds) {
    NSString* script = GetHelperPath(@"mediaremote-adapter", @"pl");
    NSString* framework = GetHelperPath(@"MediaRemoteAdapter", @"framework");

    if (!script || !framework)
        return;

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        auto micros = static_cast<long long>(seconds * 1e6);
        NSArray* args = @[script, framework, @"seek", [NSString stringWithFormat:@"%lld", micros]];

        NSTask* task = [[NSTask alloc] init];
        [task setLaunchPath:@"/usr/bin/perl"];
        [task setArguments:args];
        [task setStandardOutput:[NSFileHandle fileHandleWithNullDevice]];
        [task setStandardError:[NSFileHandle fileHandleWithNullDevice]];

        if ([task launchAndReturnError:nil])
            [task waitUntilExit];
    });
}
