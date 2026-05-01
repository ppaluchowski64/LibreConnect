#include "MediaTrackInfo.h"

#include <optional>

// macOS API is too restricted rn, skipping for now
std::optional<TrackMetadata> MediaTrackInfo::GetCurrentTrack() {
    return std::nullopt;
}

void MediaTrackInfo::SetPosition(double /*seconds*/) {
    // do nothing for now
}
