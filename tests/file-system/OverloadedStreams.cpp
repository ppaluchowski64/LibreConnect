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
    os << "Name: " << (entry.GetName() ? *entry.GetName() : "Unknown") << '\n';
    os << "Path: " << (entry.GetPath() ? *entry.GetPath() : "Unknown") << '\n';

    if (auto size = entry.GetSize())
        os << "Size: " << *size << " bytes\n";
    else
        os << "Size: Unknown\n";

    os << "Type: " << (entry.GetType() ? *entry.GetType() : FileType::Unknown) << '\n';

    std::tm tm{};

    if (auto modTime = entry.GetLastModTime()) {
        auto time = *modTime;
        tm = *std::localtime(&time);
        os << "Last Modification Time: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '\n';
    } else {
        os << "Last Modification Time: Unknown\n";
    }

    if (auto createTime = entry.GetCreationTime()) {
        auto time = *createTime;
        tm = *std::localtime(&time);
        os << "Creation Time: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '\n';
    } else {
        os << "Creation Time: Unknown\n";
    }

    return os;
}
