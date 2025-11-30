#include <iostream>
#include "FileEntry.h"

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
    std::cout << "Size: " << entry.size << '\n';
    std::cout << "Type: " << entry.type << '\n';
    std::cout << "Last Modification Time: " << entry.lastModTime << '\n';
}

int main() {
    FileEntry original;
    original.name = "example.flac";
    original.path = "/home/user";
    original.size = 1024;
    original.type = DetectFileType(original.name);
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
