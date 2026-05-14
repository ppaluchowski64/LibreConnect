#include "MediaTrackInfo.h"

#include <fstream>
#include <chrono>
#include <algorithm>

bool MediaTrackInfo::SaveCoverToFile(const TrackMetadata& metadata, const std::string& path) {
    if (metadata.cover.empty())
        return false;

    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;

    file.write(reinterpret_cast<const char*>(metadata.cover.data()),
               static_cast<std::streamsize>(metadata.cover.size()));

    return true;
}

double MediaTrackInfo::CalculateInterpolatedPosition(double rawPosition, int64_t lastUpdateMicros, bool isPlaying) {
    if (!isPlaying || lastUpdateMicros <= 0)
        return rawPosition;

    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int64_t diff = std::max<int64_t>(0, now - lastUpdateMicros);
    return rawPosition + (static_cast<double>(diff) / 1000000.0);
}
