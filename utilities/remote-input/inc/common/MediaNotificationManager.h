#ifndef MEDIA_NOTIFICATION_MANAGER_H
#define MEDIA_NOTIFICATION_MANAGER_H

#include "InputTypes.h"
#include "MediaTrackInfo.h"

#include <functional>

class MediaNotificationManager {
    public:
        MediaNotificationManager() = delete;

        static void Show();
        static void Hide();

        static void UpdateMetadata(const TrackMetadata& metadata);
        static void UpdatePlaybackState(bool isPlaying, double position);

        static void SetActionCallback(const std::function<void(MediaSignal)>& callback);
        static void SetSeekCallback(const std::function<void(double)>& callback);

        static void InvokeAction(MediaSignal signal);
        static void InvokeSeek(double position);
};

#endif // MEDIA_NOTIFICATION_MANAGER_H
