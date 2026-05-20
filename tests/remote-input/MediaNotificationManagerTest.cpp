#include "MediaNotificationManager.h"

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cmath>

#ifdef __linux__
    #include <QCoreApplication>
#endif

std::mutex g_metaMutex;
TrackMetadata g_meta;
int64_t g_lastUpdateMicros = 0;
std::atomic<const char*> g_currentPrompt{nullptr};

void SyncTestPosition() {
    int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    if (g_lastUpdateMicros > 0) {
        g_meta.position = MediaTrackInfo::CalculateInterpolatedPosition(g_meta.position, g_lastUpdateMicros, g_meta.playing);

        if (g_meta.duration > 0.0 && g_meta.position >= g_meta.duration) {
            g_meta.position = std::fmod(g_meta.position, g_meta.duration);
            MediaNotificationManager::UpdatePlaybackState(g_meta.playing, g_meta.position);
        }
    }

    g_lastUpdateMicros = now;
}

void PlaybackMonitorLoop(const std::atomic<bool>& running) {
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::lock_guard<std::mutex> lock(g_metaMutex);

        if (g_meta.playing && g_meta.duration > 0.0) {
            double currentPos = MediaTrackInfo::CalculateInterpolatedPosition(g_meta.position, g_lastUpdateMicros, g_meta.playing);

            if (currentPos >= g_meta.duration)
                SyncTestPosition();
        }
    }
}

void PrintMenu() {
    std::cout << "\n--- MEDIA NOTIFICATION TEST ---\n"
              << "[0] Show Notification\n"
              << "[1] Hide Notification\n"
              << "[2] Set Title\n"
              << "[3] Set Artist\n"
              << "[4] Set Album\n"
              << "[5] Set Duration (seconds)\n"
              << "[6] Set Position (seconds)\n"
              << "[7] Toggle Play/Pause\n"
              << "[8] Set Cover (file path)\n"
              << "[9] Exit\n> ";

    std::cout.flush();
}

std::vector<uint8_t> LoadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);

    if (!file)
        return {};

    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void InputLoop(std::atomic<bool>& running) {
    int choice;
    std::string buffer;

    while (running && std::cin >> choice) {
        std::getline(std::cin, buffer);

        switch (choice) {
            case 0: {
                std::lock_guard<std::mutex> lock(g_metaMutex);
                SyncTestPosition();

                MediaNotificationManager::Show();
                MediaNotificationManager::UpdateMetadata(g_meta);
                MediaNotificationManager::UpdatePlaybackState(g_meta.playing, g_meta.position);
                std::cout << "Notification shown.\n";

                break;
            }

            case 1:
                MediaNotificationManager::Hide();
                std::cout << "Notification hidden.\n";

                break;

            case 2:
                g_currentPrompt = "Enter new Title: ";
                std::cout << g_currentPrompt;
                std::getline(std::cin, buffer);
                g_currentPrompt = nullptr;

                {
                    std::lock_guard<std::mutex> lock(g_metaMutex);
                    g_meta.title = buffer;
                    MediaNotificationManager::UpdateMetadata(g_meta);
                }

                break;

            case 3:
                g_currentPrompt = "Enter new Artist: ";
                std::cout << g_currentPrompt;
                std::getline(std::cin, buffer);
                g_currentPrompt = nullptr;

                {
                    std::lock_guard<std::mutex> lock(g_metaMutex);
                    g_meta.artist = buffer;
                    MediaNotificationManager::UpdateMetadata(g_meta);
                }

                break;

            case 4:
                g_currentPrompt = "Enter new Album: ";
                std::cout << g_currentPrompt;
                std::getline(std::cin, buffer);
                g_currentPrompt = nullptr;

                {
                    std::lock_guard<std::mutex> lock(g_metaMutex);
                    g_meta.album = buffer;
                    MediaNotificationManager::UpdateMetadata(g_meta);
                }

                break;

            case 5: {
                g_currentPrompt = "Enter new Duration (s): ";
                std::cout << g_currentPrompt;

                double newDuration;

                if (std::cin >> newDuration) {
                    std::lock_guard<std::mutex> lock(g_metaMutex);
                    SyncTestPosition();

                    g_meta.duration = newDuration;

                    if (g_meta.duration > 0.0 && g_meta.position > g_meta.duration) {
                        g_meta.position = g_meta.duration;
                        g_lastUpdateMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        MediaNotificationManager::UpdatePlaybackState(g_meta.playing, g_meta.position);
                    }

                    MediaNotificationManager::UpdateMetadata(g_meta);
                }

                g_currentPrompt = nullptr;

                break;
            }

            case 6: {
                g_currentPrompt = "Enter new Position (s): ";
                std::cout << g_currentPrompt;

                double newPosition;

                if (std::cin >> newPosition) {
                    std::lock_guard<std::mutex> lock(g_metaMutex);
                    g_meta.position = newPosition;

                    if (g_meta.duration > 0.0 && g_meta.position > g_meta.duration)
                        g_meta.position = g_meta.duration;

                    g_lastUpdateMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    MediaNotificationManager::UpdatePlaybackState(g_meta.playing, g_meta.position);
                }

                g_currentPrompt = nullptr;

                break;
            }

            case 7: {
                std::lock_guard<std::mutex> lock(g_metaMutex);
                SyncTestPosition();

                g_meta.playing = !g_meta.playing;
                MediaNotificationManager::UpdatePlaybackState(g_meta.playing, g_meta.position);
                std::cout << "State changed to: " << (g_meta.playing ? "Playing" : "Paused") << "\n";

                break;
            }

            case 8: {
                g_currentPrompt = "Enter image file path (JPG/PNG): ";
                std::cout << g_currentPrompt;
                std::getline(std::cin, buffer);
                g_currentPrompt = nullptr;

                std::vector<uint8_t> coverData = LoadFile(buffer);

                std::lock_guard<std::mutex> lock(g_metaMutex);

                if (!coverData.empty()) {
                    g_meta.cover = std::move(coverData);
                    MediaNotificationManager::UpdateMetadata(g_meta);
                    std::cout << "Cover loaded successfully (" << g_meta.cover.size() << " bytes).\n";
                } else {
                    std::cout << "Failed to load cover from path: " << buffer << "\n";
                }

                break;
            }

            case 9:
                running = false;

                #ifdef __linux__
                    QCoreApplication::quit();
                #endif

                return;

            default:
                std::cout << "Unknown option.\n";
        }

        PrintMenu();
    }

    running = false;

    #ifdef __linux__
        QCoreApplication::quit();
    #endif
}

int main(int argc, char** argv) {
    #ifdef __linux__
        QCoreApplication app(argc, argv);
    #endif

    g_meta.title = "Unknown Title";
    g_meta.artist = "Unknown Artist";
    g_meta.album = "Unknown Album";
    g_meta.duration = 180.0;
    g_meta.position = 0.0;
    g_meta.playing = true;

    g_lastUpdateMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    MediaNotificationManager::SetActionCallback([&](MediaSignal signal) {
        std::lock_guard<std::mutex> lock(g_metaMutex);

        if (signal == MediaSignal::PlayPause) {
            std::cout << "\nPlay/Pause detected\n";

            SyncTestPosition();
            g_meta.playing = !g_meta.playing;

            MediaNotificationManager::UpdatePlaybackState(g_meta.playing, g_meta.position);
        } else if (signal == MediaSignal::NextTrack) {
            std::cout << "\nNext Track detected\n";

            g_meta.position = 0.0;
            g_lastUpdateMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

            MediaNotificationManager::UpdatePlaybackState(g_meta.playing, g_meta.position);
        } else if (signal == MediaSignal::PreviousTrack) {
            std::cout << "\nPrevious Track detected\n";

            g_meta.position = 0.0;
            g_lastUpdateMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

            MediaNotificationManager::UpdatePlaybackState(g_meta.playing, g_meta.position);
        }

        if (const char* prompt = g_currentPrompt.load())
            std::cout << prompt;
        else
            std::cout << "> ";

        std::cout.flush();
    });

    MediaNotificationManager::SetSeekCallback([&](double pos) {
        std::lock_guard<std::mutex> lock(g_metaMutex);

        std::cout << "\nPosition Setting " << pos << " detected\n";
        g_meta.position = pos;
        g_lastUpdateMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        MediaNotificationManager::UpdatePlaybackState(g_meta.playing, g_meta.position);

        if (const char* prompt = g_currentPrompt.load())
            std::cout << prompt;
        else
            std::cout << "> ";

        std::cout.flush();
    });

    MediaNotificationManager::Show();
    MediaNotificationManager::UpdateMetadata(g_meta);
    MediaNotificationManager::UpdatePlaybackState(g_meta.playing, g_meta.position);

    PrintMenu();

    std::atomic<bool> running{true};
    std::thread inputThread(InputLoop, std::ref(running));
    std::thread monitorThread(PlaybackMonitorLoop, std::ref(running));

    #ifdef __linux__
        app.exec();
    #else
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    #endif

    if (inputThread.joinable())
        inputThread.join();

    if (monitorThread.joinable())
        monitorThread.join();

    MediaNotificationManager::Hide();

    return 0;
}
