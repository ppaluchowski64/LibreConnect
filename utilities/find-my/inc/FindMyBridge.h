#ifndef FIND_MY_BRIDGE_H
#define FIND_MY_BRIDGE_H

#ifdef ANDROID_DEVICE

    #include <string>

    class FindMyBridge {
    public:
        static void StartAlert(const std::string& customUri = "");
        static void StopAlert();
    };

#endif

#endif