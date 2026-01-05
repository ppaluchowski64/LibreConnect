#include "OverloadedStreams.h"

#include <iomanip>
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

std::ostream& operator<<(std::ostream& os, const FileEntry& entry) {
    os << "Name: " << entry.name << '\n';
    os << "Path: " << entry.path << '\n';
    os << "Size: " << entry.size << " bytes\n";
    os << "Type: " << entry.type << '\n';

    std::tm tm{};

    if (entry.lastModTime) {
        auto time = static_cast<std::time_t>(*entry.lastModTime);
        tm = *std::localtime(&time);
        os << "Last Modification Time: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '\n';
    } else {
        os << "Last Modification Time: Unknown\n";
    }

    if (entry.creationTime) {
        auto time = static_cast<std::time_t>(*entry.creationTime);
        tm = *std::localtime(&time);
        os << "Last Creation Time: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '\n';
    } else {
        os << "Creation Time: Unknown\n";
    }

    return os;
}
