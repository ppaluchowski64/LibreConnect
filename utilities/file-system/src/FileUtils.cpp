#include "FileUtils.h"
#include "FileEntry.h"

#include <optional>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <unordered_set>

#ifdef __linux__
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/syscall.h>
    #include <linux/stat.h>
#endif

std::optional<FileType> DetectFileType(const std::filesystem::path& path) {
    if (std::filesystem::is_directory(path))
        return FileType::Directory;

    const auto dotExt = path.extension().string();
    if (dotExt.empty())
        return FileType::Unknown;

    std::string ext = dotExt.substr(1);
    for (char& c : ext) {
        c = std::tolower(static_cast<unsigned char>(c));
    }

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

std::optional<std::time_t> GetFileLastModTime(const std::filesystem::path& path) {
    try {
        const auto ftime = std::filesystem::last_write_time(path);

        const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now()
            + std::chrono::system_clock::now()
        );

        return std::chrono::system_clock::to_time_t(sctp);
    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    }
}

std::optional<std::time_t> GetFileCreationTime(const std::filesystem::path& path) {
    #ifdef __linux__
        struct statx stx{};
        int dirfd = AT_FDCWD;

        auto ret = syscall(
            SYS_statx,
            dirfd,
            path.c_str(),
            AT_SYMLINK_NOFOLLOW,
            STATX_BTIME,
            &stx
        );

        if (ret == 0 && (stx.stx_mask & STATX_BTIME))
            return stx.stx_btime.tv_sec;

        return std::nullopt;
    #else
        return std::nullopt;
    #endif
}
