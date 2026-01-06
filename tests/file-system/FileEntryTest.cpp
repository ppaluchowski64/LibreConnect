#include "FileEntry.h"
#include "OverloadedStreams.h"

#include <vector>
#include <iostream>
#include <fstream>
#include <filesystem>

int main() {
    std::filesystem::path testDir = "test_dir";
    std::filesystem::path testFile = testDir / "test.txt";

    bool fileCreated = !(std::filesystem::exists(testDir) && std::filesystem::exists(testFile));

    if (!std::filesystem::exists(testDir)) {
        std::filesystem::create_directory(testDir);
        std::cout << "Created directory: " << testDir << '\n';
    }

    if (!std::filesystem::exists(testFile)) {
        std::ofstream file(testFile);
        file << "Just a test content :)\n";
        std::cout << "Created file: " << testFile << '\n';
    }

    if (fileCreated)
        std::cout << '\n';

    std::vector<uint8_t> buffer;
    size_t offset = 0;

    FileEntry originalFile(testFile);

    std::cout << "[ORIGINAL FILE]\n";
    std::cout << originalFile << '\n';

    buffer.resize(originalFile.GetSerializedSize());
    originalFile.Serialize(buffer, offset);

    FileEntry restoredFile;
    offset = 0;
    restoredFile.Deserialize(buffer, offset);

    std::cout << "[RESTORED FILE]\n";
    std::cout << restoredFile << '\n';

    FileEntry originalDir(testDir);

    std::cout << "[ORIGINAL DIRECTORY]\n";
    std::cout << originalDir << '\n';

    offset = 0;
    buffer.resize(originalDir.GetSerializedSize());
    originalDir.Serialize(buffer, offset);

    FileEntry restoredDir;
    offset = 0;
    restoredDir.Deserialize(buffer, offset);

    std::cout << "[RESTORED DIRECTORY]\n";
    std::cout << restoredDir;

    return 0;
}
