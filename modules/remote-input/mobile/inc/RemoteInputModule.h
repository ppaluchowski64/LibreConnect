#ifndef REMOTEINPUTMODULE_H
#define REMOTEINPUTMODULE_H

#include <BaseModule.h>
#include <InputTypes.h>
#include <MediaRemote.h>

class RemoteInputModule : public BaseModule {
public:
    static void SendInput(Key key, InputEventType type);
    static void SendMediaInput(MediaSignal signal);
    static void RequestMediaInfo();
    static void SetMediaPosition(double seconds);
    static void SetVolume(int volume);
    static void SendMediaInfoUpdate(
        const std::string& title,
        const std::string& artist,
        const std::string& collection,
        const std::string& elapsed,
        bool playing,
        double positionSeconds,
        double durationSeconds,
        const std::vector<uint8_t>& coverBytes
    );
    static void SetMirroringEnabled(bool enabled);
    static bool IsMirroringEnabled();

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;

    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

    const char* GetModuleName() const override;
    ModuleType GetModuleType() const override;

private:
    MediaRemote m_remote{};
};

#endif // REMOTEINPUTMODULE_H
