#include <iostream>
#include "FileEntry.h"

void printEntry(const FileEntry& entry) {
    std::cout << "Name: " << entry.name << '\n';
    std::cout << "Path: " << entry.path << '\n';
    std::cout << "Size: " << entry.size << '\n';
    std::cout << "Type: " << static_cast<int>(entry.type) << '\n';
    std::cout << "Last Modification Time: " << entry.lastModTime << '\n';
}

int main() {
    FileEntry original;
    original.name = "example.txt";
    original.path = "/home/user";
    original.size = 1024;
    original.type = FileType::Text;
    original.lastModTime = 1500000000;

    std::cout << "[ORIGINAL]\n";
    printEntry(original);

    std::vector<uint8_t> buffer;
    size_t offset = 0;
    buffer.resize(original.GetSerializedSize());
    original.Serialize(buffer, offset);

    FileEntry restored;
    offset = 0;
    restored.Deserialize(buffer, offset);

    std::cout << "\n[RESTORED]\n";
    printEntry(restored);

    return 0;
}
