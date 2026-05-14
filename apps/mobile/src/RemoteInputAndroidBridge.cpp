#ifdef ANDROID_DEVICE

#include <jni.h>
#include <string>

#include <RemoteInputModule.h>

extern "C" {
JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_RemoteInputMediaActionReceiver_onRemoteInputMediaActionCPP(
    JNIEnv* env,
    jobject /*receiver*/,
    jstring action
) {
    if (env == nullptr || action == nullptr) {
        return;
    }

    const char* actionChars = env->GetStringUTFChars(action, nullptr);
    if (actionChars == nullptr) {
        return;
    }

    const std::string actionName(actionChars);
    env->ReleaseStringUTFChars(action, actionChars);

    if (actionName == "previous") {
        RemoteInputModule::SendMediaInput(MediaSignal::PreviousTrack);
    } else if (actionName == "play_pause") {
        RemoteInputModule::SendMediaInput(MediaSignal::PlayPause);
    } else if (actionName == "next") {
        RemoteInputModule::SendMediaInput(MediaSignal::NextTrack);
    } else if (actionName == "volume_down") {
        RemoteInputModule::SendMediaInput(MediaSignal::VolumeDown);
    } else if (actionName == "volume_up") {
        RemoteInputModule::SendMediaInput(MediaSignal::VolumeUp);
    }
}

JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_RemoteInputMediaActionReceiver_onRemoteInputMediaSeekCPP(
    JNIEnv* /*env*/,
    jobject /*receiver*/,
    jdouble positionSeconds
) {
    RemoteInputModule::SetMediaPosition(static_cast<double>(positionSeconds));
}
}

#endif
