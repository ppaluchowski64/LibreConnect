#include "MediaTrackInfo.h"
#include <DebugLog.h>

#import <Foundation/Foundation.h>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>

using json = nlohmann::json;

namespace {
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

    std::optional<TrackMetadata> ParseMetadata(const json& j) {
        if (j.is_null())
            return std::nullopt;

        TrackMetadata info{};

        if (j.contains("title") && !j["title"].is_null())
            info.title = j["title"];

        if (j.contains("artist") && !j["artist"].is_null())
            info.artist = j["artist"];

        if (j.contains("album") && !j["album"].is_null())
            info.album = j["album"];

        if (j.contains("duration") && !j["duration"].is_null())
            info.duration = j["duration"];

        if (j.contains("elapsedTime") && !j["elapsedTime"].is_null())
            info.position = j["elapsedTime"];

        if (j.contains("playing") && !j["playing"].is_null())
            info.playing = j["playing"];

        if (j.contains("artworkData") && !j["artworkData"].is_null()) {
            NSData* art = [[NSData alloc] initWithBase64EncodedString:@(std::string(j["artworkData"]).c_str()) options:0];

            if (art) {
                auto bytes = static_cast<const uint8_t*>([art bytes]);
                info.cover.assign(bytes, bytes + [art length]);
            }
        }

        return info;
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
                return m_state;
            }

        private:
            MediaWorker() = default;

            void StartIfNeeded() {
                if (m_running)
                    return;

                NSString* script = GetHelperPath(@"mediaremote-adapter", @"pl");
                NSString* framework = GetHelperPath(@"MediaRemoteAdapter", @"framework");

                if (!script || !framework)
                    return;

                m_task = [[NSTask alloc] init];
                [m_task setLaunchPath:@"/usr/bin/perl"];
                [m_task setArguments:@[script, framework, @"stream", @"--no-diff", @"--debounce=100"]];

                m_pipe = [NSPipe pipe];
                [m_task setStandardOutput:m_pipe];
                [m_task setStandardError:[NSFileHandle fileHandleWithNullDevice]];

                if (![m_task launchAndReturnError:nil])
                    return;

                m_running = true;

                std::thread([this] {
                    ProcessStream();
                }).detach();
            }

            void ProcessStream() {
                NSFileHandle* handle = [m_pipe fileHandleForReading];
                std::string buffer;

                while (m_running) {
                    @autoreleasepool {
                        NSData* data = [handle availableData];

                        if (!data || [data length] == 0) {
                            m_running = false;
                            break;
                        }

                        buffer.append(static_cast<const char*>([data bytes]), [data length]);

                        size_t pos;

                        while ((pos = buffer.find('\n')) != std::string::npos) {
                            std::string line = buffer.substr(0, pos);
                            buffer.erase(0, pos + 1);

                            if (line.empty())
                                continue;

                            try {
                                auto j = json::parse(line);
                                auto newState = ParseMetadata(j["payload"]);

                                std::unique_lock lock(m_mutex);
                                m_state = newState;

                            } catch (...) {}
                        }
                    }
                }
            }

            std::mutex m_mutex;
            std::optional<TrackMetadata> m_state;
            std::atomic<bool> m_running{false};
            NSTask* m_task = nil;
            NSPipe* m_pipe = nil;
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

    auto micros = static_cast<long long>(seconds * 1e6);
    NSArray* args = @[script, framework, @"seek", [@(micros) stringValue]];

    NSTask* task = [[NSTask alloc] init];
    [task setLaunchPath:@"/usr/bin/perl"];
    [task setArguments:args];
    [task setStandardOutput:[NSFileHandle fileHandleWithNullDevice]];
    [task setStandardError:[NSFileHandle fileHandleWithNullDevice]];

    if ([task launchAndReturnError:nil])
        [task waitUntilExit];
}
