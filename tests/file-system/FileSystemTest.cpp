#include "FileSystemManager.h"
#include "OverloadedStreams.h"

#include <vector>
#include <iostream>
#include <fstream>
#include <filesystem>

#include <QGuiApplication>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    std::cout << "[APP DATA PATH]\n";

    const std::string appName = "LibreConnect";
    const auto appDataPath = FileSystemManager::GetAppDataPath(appName);

    if (appDataPath.empty()) {
        std::cout << "Failed to get application data path\n";
        return 1;
    }

    std::cout << "App data path: " << appDataPath << "\n\n";

    const std::filesystem::path baseDir = appDataPath / "fs_test";
    const std::filesystem::path copyDir = baseDir / "copy";
    const std::filesystem::path testFile = baseDir / "test.txt";

    std::filesystem::create_directories(copyDir);

    if (!std::filesystem::exists(testFile)) {
        std::ofstream file(testFile);
        file << "Just a test content :)\n";
    }

    std::cout << "[ENTRIES IN " << baseDir << "]\n\n";
    const auto result = FileSystemManager::GetEntries(baseDir);

    if (!result.success) {
        std::cout << "Failed to access directory: " << baseDir << '\n';
    }
    else if (result.entries.empty()) {
        std::cout << "Directory is empty: " << baseDir << '\n';
    }
    else {
        for (const auto& entry : result.entries) {
            std::cout << entry << '\n';
        }
    }

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
