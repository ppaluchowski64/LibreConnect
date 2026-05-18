#include "MediaNotificationManager.h"

void MediaNotificationManager::Show() {}
void MediaNotificationManager::Hide() {}

void MediaNotificationManager::UpdateMetadata(const TrackMetadata& /*metadata*/) {}
void MediaNotificationManager::UpdatePlaybackState(bool /*isPlaying*/, double /*position*/) {}

void MediaNotificationManager::SetActionCallback(const std::function<void(MediaSignal)>& /*callback*/) {}
void MediaNotificationManager::SetSeekCallback(const std::function<void(double)>& /*callback*/) {}
