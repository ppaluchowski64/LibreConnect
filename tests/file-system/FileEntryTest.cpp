#include "FileEntry.h"
#include "OverloadedStreams.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <ctime>

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

    if (!(std::filesystem::exists(testDir) && std::filesystem::exists(testFile)))
        std::cout << '\n';

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

    std::cout << "[ORIGINAL FILE]\n";
    std::cout << original << "\n\n";

    std::vector<uint8_t> buffer;
    size_t offset = 0;
    buffer.resize(original.GetSerializedSize());
    original.Serialize(buffer, offset);

    FileEntry restored;
    offset = 0;
    restored.Deserialize(buffer, offset);

    std::cout << "[RESTORED FILE]\n";
    std::cout << restored << "\n\n";

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

    std::cout << "[ORIGINAL DIRECTORY]\n";
    std::cout << original << "\n\n";

    offset = 0;
    buffer.resize(original.GetSerializedSize());
    original.Serialize(buffer, offset);

    offset = 0;
    restored.Deserialize(buffer, offset);

    std::cout << "[RESTORED DIRECTORY]\n";
    std::cout << restored << '\n';

    return 0;
}
