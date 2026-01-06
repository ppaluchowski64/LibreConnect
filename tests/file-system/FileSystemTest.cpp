#include "FileSystemManager.h"
#include "OverloadedStreams.h"

#include <iostream>
#include <filesystem>

int main() {
    const std::filesystem::path testDir = "test_dir";

    const auto result = FileSystemManager::GetEntries(testDir);

    if (!result.success) {
        std::cout << "Failed to access directory: " << testDir << '\n';
        return 1;
    }

    if (result.entries.empty()) {
        std::cout << "Directory is empty: " << testDir << '\n';
        return 0;
    }

    std::cout << "\n[ENTRIES IN " << testDir << "]\n\n";
    for (const auto& entry : result.entries) {
        std::cout << entry << '\n';
    }

    const std::string appName = "LibreConnect";
    const auto appDataPath = FileSystemManager::GetAppDataPath(appName);

    std::cout << "[APP DATA PATH]\n";

    if (appDataPath.empty()) {
        std::cout << "Failed to get application data path\n";
        return 1;
    }

    std::cout << "App data path: " << appDataPath << '\n';

    return 0;
}
