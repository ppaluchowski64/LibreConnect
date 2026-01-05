#include "FileTimeUtils.h"

#include <optional>
#include <filesystem>
#include <chrono>
#include <ctime>

#ifdef __linux__
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/syscall.h>
    #include <linux/stat.h>
#endif

std::optional<std::time_t> GetLastModTime(const std::filesystem::path& path) {
    try {
        auto ftime = std::filesystem::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now()
            + std::chrono::system_clock::now()
        );
        return std::chrono::system_clock::to_time_t(sctp);
    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    }
}

std::optional<std::time_t> GetCreationTime(const std::filesystem::path& path) {
    #ifdef __linux__
        struct statx stx{};
        int dirfd = AT_FDCWD;

        int ret = syscall(
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
