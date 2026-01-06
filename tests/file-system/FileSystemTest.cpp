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

    return 0;
}
