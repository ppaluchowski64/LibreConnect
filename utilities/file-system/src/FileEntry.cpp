#include "FileEntry.h"

#include <Packable.h>
#include <filesystem>
#include <unordered_set>

FileType DetectFileType(const std::string& filepath) {
    if (std::filesystem::is_directory(filepath))
        return FileType::Directory;

    const size_t pos = filepath.find_last_of('.');
    if (pos == std::string::npos)
        return FileType::Unknown;

    const std::string ext = filepath.substr(pos + 1);

    static const std::unordered_set<std::string> TEXT = {
        "txt", "log", "md", "markdown", "csv", "tsv", "json",
        "xml", "yaml", "yml", "ini", "cfg", "rtf", "html", "htm",
        "sgml", "tex", "adoc", "rst"
    };

    static const std::unordered_set<std::string> IMAGE = {
        "png", "jpg", "jpeg", "bmp", "gif", "tiff", "tif",
        "webp", "svg", "ico", "heic", "heif", "raw", "cr2",
        "nef", "orf", "arw", "psd", "xcf", "ai", "eps",
        "dng", "ppm", "pgm", "pbm", "pcx", "tga", "exr",
        "hdr"
    };

    static const std::unordered_set<std::string> VIDEO = {
        "mp4", "mkv", "mov", "avi", "wmv", "flv", "webm",
        "mpeg", "mpg", "3gp", "m4v", "ts", "mts", "m2ts",
        "vob", "f4v", "mxf", "ogv", "rm", "rmvb", "asf",
        "divx", "xvid", "flv1", "movx", "mjpeg"
    };

    static const std::unordered_set<std::string> AUDIO = {
        "mp3", "wav", "ogg", "flac", "aac", "m4a", "wma",
        "opus", "amr", "aiff", "alac", "mid", "midi", "caf",
        "dsf", "dff", "pcm", "ra", "mp2", "ac3", "eac3",
        "wv", "tta"
    };

    static const std::unordered_set<std::string> DOCUMENT = {
        "pdf", "doc", "docx", "odt", "xls", "xlsx", "ods",
        "ppt", "pptx", "odp", "epub", "djvu", "cbz", "cbr",
        "opf", "xps", "pages", "numbers", "key", "vsdx", "odg",
        "odf", "fods", "sxc", "sxi", "dox", "mobi", "lit",
        "azw", "azw3", "cbt", "cba"
    };

    static const std::unordered_set<std::string> ARCHIVE = {
        "zip", "rar", "7z", "tar", "gz", "bz2", "xz",
        "tgz", "iso", "cab", "ar", "lz", "lzma", "z",
        "jar", "war", "ear", "cpio", "shar", "ace", "uue",
        "bz", "xz2"
    };

    static const std::unordered_set<std::string> EXECUTABLE = {
        "exe", "dll", "so", "dylib", "bin", "o", "obj",
        "wasm", "out", "app", "elf", "a", "lib", "dmg",
        "msi", "vxd", "sys", "drv", "ipa", "apk", "x",
        "deb", "rpm", "bat", "cmd", "sh"
    };

    if (TEXT.contains(ext))
        return FileType::Text;
    if (IMAGE.contains(ext))
        return FileType::Image;
    if (VIDEO.contains(ext))
        return FileType::Video;
    if (AUDIO.contains(ext))
        return FileType::Audio;
    if (DOCUMENT.contains(ext))
        return FileType::Document;
    if (ARCHIVE.contains(ext))
        return FileType::Archive;
    if (EXECUTABLE.contains(ext))
        return FileType::Executable;

    return FileType::Unknown;
}

// Placeholder for implementation of the function to calculate directory size

void FileEntry::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(name, buffer, offset);
    SerializeObject(path, buffer, offset);
    SerializeObject(size, buffer, offset);
    SerializeObject(type, buffer, offset);
    SerializeObject(lastModTime, buffer, offset);
    SerializeObject(creationTime, buffer, offset);
}

void FileEntry::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(name, buffer, offset);
    DeserializeObject(path, buffer, offset);
    DeserializeObject(size, buffer, offset);
    DeserializeObject(type, buffer, offset);
    DeserializeObject(lastModTime, buffer, offset);
    DeserializeObject(creationTime, buffer, offset);
}

size_t FileEntry::GetSerializedSize() const {
    return GetObjectSerializedSize(name) +
           GetObjectSerializedSize(path) +
           GetObjectSerializedSize(size) +
           GetObjectSerializedSize(type) +
           GetObjectSerializedSize(lastModTime) +
           GetObjectSerializedSize(creationTime);
}
