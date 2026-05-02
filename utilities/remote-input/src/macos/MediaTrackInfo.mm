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
#include <chrono>

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

    struct ExtractedData {
        TrackMetadata info;
        long long timestampEpochMicros = 0;
        long long elapsedTimeMicros = 0;
        double playbackRate = 0.0;
    };

    std::optional<ExtractedData> ParseMetadata(const json& j) {
        if (j.is_null())
            return std::nullopt;

        ExtractedData data{};

        if (j.contains("title") && !j["title"].is_null())
            data.info.title = j["title"];

        if (j.contains("artist") && !j["artist"].is_null())
            data.info.artist = j["artist"];

        if (j.contains("album") && !j["album"].is_null())
            data.info.album = j["album"];

        if (j.contains("durationMicros") && !j["durationMicros"].is_null())
            data.info.duration = static_cast<double>(j["durationMicros"].get<long long>()) / 1e6;

        if (j.contains("elapsedTimeMicros") && !j["elapsedTimeMicros"].is_null()) {
            data.elapsedTimeMicros = j["elapsedTimeMicros"].get<long long>();
            data.info.position = static_cast<double>(data.elapsedTimeMicros) / 1e6;
        }

        if (j.contains("playing") && !j["playing"].is_null())
            data.info.playing = j["playing"];

        if (j.contains("timestampEpochMicros") && !j["timestampEpochMicros"].is_null())
            data.timestampEpochMicros = j["timestampEpochMicros"].get<long long>();

        if (j.contains("playbackRate") && !j["playbackRate"].is_null())
            data.playbackRate = j["playbackRate"].get<double>();

        if (j.contains("artworkData") && !j["artworkData"].is_null()) {
            NSData* art = [[NSData alloc] initWithBase64EncodedString:@(std::string(j["artworkData"]).c_str()) options:0];

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
                if (m_running)
                    return;

                NSString* script = GetHelperPath(@"mediaremote-adapter", @"pl");
                NSString* framework = GetHelperPath(@"MediaRemoteAdapter", @"framework");

                if (!script || !framework)
                    return;

                m_task = [[NSTask alloc] init];
                [m_task setLaunchPath:@"/usr/bin/perl"];
                [m_task setArguments:@[script, framework, @"stream", @"--no-diff", @"--debounce=100", @"--micros"]];

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
            std::optional<ExtractedData> m_state;
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
    NSArray* args = @[script, framework, @"seek", [NSString stringWithFormat:@"%lld", micros]];

    NSTask* task = [[NSTask alloc] init];
    [task setLaunchPath:@"/usr/bin/perl"];
    [task setArguments:args];
    [task setStandardOutput:[NSFileHandle fileHandleWithNullDevice]];
    [task setStandardError:[NSFileHandle fileHandleWithNullDevice]];

    if ([task launchAndReturnError:nil])
        [task waitUntilExit];
}
