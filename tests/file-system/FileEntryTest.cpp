#include "FileEntry.h"
#include "FileTimeUtils.h"
#include "OverloadedStreams.h"

#include <iostream>
#include <fstream>
#include <filesystem>

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
    original.lastModTime = GetLastModTime(testFile);
    original.creationTime = GetCreationTime(testFile);

    std::cout << "[ORIGINAL FILE]\n";
    std::cout << original << '\n';

    std::vector<uint8_t> buffer;
    size_t offset = 0;
    buffer.resize(original.GetSerializedSize());
    original.Serialize(buffer, offset);

    FileEntry restored;
    offset = 0;
    restored.Deserialize(buffer, offset);

    std::cout << "[RESTORED FILE]\n";
    std::cout << restored << '\n';

    original.name = testDir.filename().string();
    original.path = std::filesystem::absolute(testDir).parent_path().string();
    original.size = 0; // Harder than I thought, will fix later, too sleepy now ;)
    // P.S. Placeholders for the solution are already in the appropriate files
    original.type = DetectFileType(testDir.string());
    original.lastModTime = GetLastModTime(testFile);
    original.creationTime = GetCreationTime(testFile);

    std::cout << "[ORIGINAL DIRECTORY]\n";
    std::cout << original << '\n';

    offset = 0;
    buffer.resize(original.GetSerializedSize());
    original.Serialize(buffer, offset);

    offset = 0;
    restored.Deserialize(buffer, offset);

    std::cout << "[RESTORED DIRECTORY]\n";
    std::cout << restored;

    return 0;
}
