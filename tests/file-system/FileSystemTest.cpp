#include "FileSystemManager.h"
#include "OverloadedStreams.h"

#include <QGuiApplication>

#include <vector>
#include <iostream>
#include <fstream>
#include <filesystem>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    const std::filesystem::path testDir = "test_dir";
    const std::filesystem::path copyDir = testDir / "copy";
    const std::filesystem::path testFile = testDir / "test.txt";

    const bool fileCreated = !(
        std::filesystem::exists(testDir)
        && std::filesystem::exists(copyDir)
        && std::filesystem::exists(testFile)
    );

    if (!std::filesystem::exists(testDir)) {
        std::filesystem::create_directory(testDir);
        std::cout << "Created directory: " << testDir << '\n';
    }

    if (!std::filesystem::exists(copyDir)) {
        std::filesystem::create_directory(copyDir);
        std::cout << "Created directory: " << copyDir << '\n';
    }

    if (!std::filesystem::exists(testFile)) {
        std::ofstream file(testFile);
        file << "Just a test content :)\n";
        std::cout << "Created file: " << testFile << '\n';
    }

    if (fileCreated)
        std::cout << '\n';

    std::cout << "[ENTRIES IN " << testDir << "]\n\n";
    const auto result = FileSystemManager::GetEntries(testDir);

    if (!result.success) {
        std::cout << "Failed to access directory: " << testDir << '\n';
    }
    else if (result.entries.empty()) {
        std::cout << "Directory is empty: " << testDir << '\n';
    }
    else {
        for (const auto& entry : result.entries) {
            std::cout << entry << '\n';
        }
    }

    std::cout << "[APP DATA PATH]\n";

    const std::string appName = "LibreConnect";
    const auto appDataPath = FileSystemManager::GetAppDataPath(appName);

    if (appDataPath.empty())
        std::cout << "Failed to get application data path\n";
    else
        std::cout << "App data path: " << appDataPath << "\n\n";

    std::cout << "[FILE CLIPBOARD]\n";

    if (FileSystemManager::CopyToClipboard(testFile)) {
        std::cout << "Files have been copied\n";

        if (FileSystemManager::FilesInClipboard()) {
            if (FileSystemManager::PasteFromClipboard(copyDir)) {
                std::cout << "Files have been pasted\n";
            } else {
                std::cout << "Something went wrong with pasting files\n";
            }
        } else {
            std::cout << "Clipboard does not contain valid file references\n";
        }
    } else {
        std::cout << "Something went wrong with copying files\n";
    }

    return 0;
}
