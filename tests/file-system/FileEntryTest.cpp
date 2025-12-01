#include "FileEntry.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <ctime>

std::ostream& operator<<(std::ostream& os, const FileType& type) {
    switch(type) {
        case FileType::Directory:
            os << "Directory"; break;
        case FileType::Text:
            os << "Text"; break;
        case FileType::Image:
            os << "Image"; break;
        case FileType::Video:
            os << "Video"; break;
        case FileType::Audio:
            os << "Audio"; break;
        case FileType::Document:
            os << "Document"; break;
        case FileType::Archive:
            os << "Archive"; break;
        case FileType::Executable:
            os << "Executable"; break;
        case FileType::Unknown:
            os << "Unknown"; break;
    }
    return os;
}

void printEntry(const FileEntry& entry) {
    std::cout << "Name: " << entry.name << '\n';
    std::cout << "Path: " << entry.path << '\n';
    std::cout << "Size: " << entry.size << " bytes\n";
    std::cout << "Type: " << entry.type << '\n';
    std::time_t modTime = static_cast<std::time_t>(entry.lastModTime);
    std::cout << "Last Modification Time: " << std::ctime(&modTime) << '\n';
}

int main() {
    std::filesystem::path testDir = "test_dir";
    std::filesystem::path testFile = testDir / "test.txt";

    if (!std::filesystem::exists(testDir)) {
        std::filesystem::create_directory(testDir);
        std::cout << "Created directory: " << testDir << '\n';
    }

    if (!std::filesystem::exists(testFile)) {
        std::ofstream file(testFile);
        file << "Just a test content :)\n";
        file.close();
        std::cout << "Created file: " << testFile << '\n';
    }

    FileEntry original;
    original.name = testFile.filename().string();
    original.path = std::filesystem::absolute(testFile).parent_path().string();
    original.size = std::filesystem::file_size(testFile);
    original.type = DetectFileType(testFile.string());
    original.lastModTime = std::chrono::system_clock::to_time_t(
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            std::filesystem::last_write_time(testFile)
            - std::filesystem::file_time_type::clock::now()
            + std::chrono::system_clock::now()
        )
    );

    std::cout << "\n[ORIGINAL FILE]\n";
    printEntry(original);

    std::vector<uint8_t> buffer;
    size_t offset = 0;
    buffer.resize(original.GetSerializedSize());
    original.Serialize(buffer, offset);

    FileEntry restored;
    offset = 0;
    restored.Deserialize(buffer, offset);

    std::cout << "[RESTORED FILE]\n";
    printEntry(restored);

    original.name = testDir.filename().string();
    original.path = std::filesystem::absolute(testDir).parent_path().string();
    original.size = 0; // Harder than I thought, will fix later, too sleepy now ;)
    original.type = DetectFileType(testDir.string());
    original.lastModTime = std::chrono::system_clock::to_time_t(
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            std::filesystem::last_write_time(testFile)
            - std::filesystem::file_time_type::clock::now()
            + std::chrono::system_clock::now()
        )
    );

    std::cout << "\n[ORIGINAL DIRECTORY]\n";
    printEntry(original);

    offset = 0;
    buffer.resize(original.GetSerializedSize());
    original.Serialize(buffer, offset);

    offset = 0;
    restored.Deserialize(buffer, offset);

    std::cout << "[RESTORED DIRECTORY]\n";
    printEntry(restored);

    return 0;
}
