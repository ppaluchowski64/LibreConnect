#include "MediaTrackInfo.h"

#include <fstream>

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
