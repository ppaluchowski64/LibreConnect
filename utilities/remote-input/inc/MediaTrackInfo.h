#ifndef MEDIA_TRACK_INFO_H
#define MEDIA_TRACK_INFO_H

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

struct TrackMetadata {
    std::string title;
    std::string artist;
    std::string album;
    double position;
    double duration;
    std::vector<uint8_t> cover;
    bool playing;
};

class MediaTrackInfo {
    public:
        static std::optional<TrackMetadata> GetCurrentTrack();
        static void SetPosition(double seconds);
        static bool SaveCoverToFile(const TrackMetadata& metadata, const std::string& path);

    private:
        static double CalculateInterpolatedPosition(double rawPosition, int64_t lastUpdateMicros, bool isPlaying);
};

#endif // MEDIA_TRACK_INFO_H
