#include "FileEntry.h"
#include <Packable.h>
#include <filesystem>

namespace fs = std::filesystem;

FileType DetectFileType(const std::string& filepath) {
    if (fs::is_directory(filepath))
        return FileType::Directory;

    const size_t pos = filepath.find_last_of('.');
    if (pos == std::string::npos)
        return FileType::Unknown;

    const std::string ext = filepath.substr(pos + 1);

    // ===== TEXT =====
    if (ext == "txt"  || ext == "log"  || ext == "md"   || ext == "markdown" ||
        ext == "csv"  || ext == "tsv"  || ext == "json" || ext == "xml"      ||
        ext == "yaml" || ext == "yml"  || ext == "ini"  || ext == "cfg"      ||
        ext == "rtf"  || ext == "html" || ext == "htm"  || ext == "sgml"     ||
        ext == "tex"  || ext == "adoc" || ext == "rst")
        return FileType::Text;

    // ===== IMAGE =====
    if (ext == "png" || ext == "jpg"  || ext == "jpeg" || ext == "bmp"  ||
        ext == "gif" || ext == "tiff" || ext == "tif"  || ext == "webp" ||
        ext == "svg" || ext == "ico"  || ext == "heic" || ext == "heif" ||
        ext == "raw" || ext == "cr2"  || ext == "nef"  || ext == "orf"  ||
        ext == "arw" || ext == "psd"  || ext == "xcf"  || ext == "ai"   ||
        ext == "eps" || ext == "dng"  || ext == "ppm"  || ext == "pgm"  ||
        ext == "pbm" || ext == "pcx"  || ext == "tga"  || ext == "exr"  ||
        ext == "hdr")
        return FileType::Image;

    // ===== VIDEO =====
    if (ext == "mp4"  || ext == "mkv"  || ext == "mov"  || ext == "avi"  ||
        ext == "wmv"  || ext == "flv"  || ext == "webm" || ext == "mpeg" ||
        ext == "mpg"  || ext == "3gp"  || ext == "m4v"  || ext == "ts"   ||
        ext == "mts"  || ext == "m2ts" || ext == "vob"  || ext == "f4v"  ||
        ext == "mxf"  || ext == "ogv"  || ext == "rm"   || ext == "rmvb" ||
        ext == "asf"  || ext == "divx" || ext == "xvid" || ext == "flv1" ||
        ext == "movx" || ext == "mjpeg")
        return FileType::Video;

    // ===== AUDIO =====
    if (ext == "mp3"  || ext == "wav"  || ext == "ogg"  || ext == "flac" ||
        ext == "aac"  || ext == "m4a"  || ext == "wma"  || ext == "opus" ||
        ext == "amr"  || ext == "aiff" || ext == "alac" || ext == "mid"  ||
        ext == "midi" || ext == "caf"  || ext == "dsf"  || ext == "dff"  ||
        ext == "pcm"  || ext == "ra"   || ext == "mp2"  || ext == "ac3"  ||
        ext == "eac3" || ext == "wv"   || ext == "tta")
        return FileType::Audio;

    // ===== DOCUMENT =====
    if (ext == "pdf"   || ext == "doc"     || ext == "docx" || ext == "odt"  ||
        ext == "xls"   || ext == "xlsx"    || ext == "ods"  || ext == "ppt"  ||
        ext == "pptx"  || ext == "odp"     || ext == "epub" || ext == "djvu" ||
        ext == "cbz"   || ext == "cbr"     || ext == "opf"  || ext == "xps"  ||
        ext == "pages" || ext == "numbers" || ext == "key"  || ext == "vsdx" ||
        ext == "odg"   || ext == "odf"     || ext == "fods" || ext == "sxc"  ||
        ext == "sxi"   || ext == "dox"     || ext == "mobi" || ext == "lit"  ||
        ext == "azw"   || ext == "azw3"    || ext == "cbt"  || ext == "cba")
        return FileType::Document;

    // ===== ARCHIVE =====
    if (ext == "zip"  || ext == "rar"    || ext == "7z"      || ext == "tar"    ||
        ext == "gz"   || ext == "bz2"    || ext == "xz"      || ext == "tgz"    ||
        ext == "iso"  || ext == "cab"    || ext == "ar"      || ext == "lz"     ||
        ext == "lzma" || ext == "z"      || ext == "jar"     || ext == "war"    ||
        ext == "ear"  || ext == "tar.gz" || ext == "tar.bz2" || ext == "tar.xz" ||
        ext == "cpio" || ext == "shar"   || ext == "ace"     || ext == "uue"    ||
        ext == "bz"   || ext == "xz2")
        return FileType::Archive;

    // ===== EXECUTABLE =====
    if (ext == "exe" || ext == "dll" || ext == "so"  || ext == "dylib" ||
        ext == "bin" || ext == "o"   || ext == "obj" || ext == "wasm"  ||
        ext == "out" || ext == "app" || ext == "elf" || ext == "a"     ||
        ext == "lib" || ext == "dmg" || ext == "msi" || ext == "vxd"   ||
        ext == "sys" || ext == "drv" || ext == "ipa" || ext == "apk"   ||
        ext == "x"   || ext == "deb" || ext == "rpm" || ext == "bat"   ||
        ext == "cmd" || ext == "sh")
        return FileType::Executable;

    return FileType::Unknown;
}

void FileEntry::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(name, buffer, offset);
    SerializeObject(path, buffer, offset);
    SerializeObject(size, buffer, offset);
    SerializeObject(type, buffer, offset);
    SerializeObject(lastModTime, buffer, offset);
}

void FileEntry::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(name, buffer, offset);
    DeserializeObject(path, buffer, offset);
    DeserializeObject(size, buffer, offset);
    DeserializeObject(type, buffer, offset);
    DeserializeObject(lastModTime, buffer, offset);
}

size_t FileEntry::GetSerializedSize() const {
    return GetObjectSerializedSize(name) +
           GetObjectSerializedSize(path) +
           GetObjectSerializedSize(size) +
           GetObjectSerializedSize(type) +
           GetObjectSerializedSize(lastModTime);
}
