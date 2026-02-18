#include <HashHelpers.h>
#include <xxhash.h>
#include <fstream>
#include <vector>
#include <DebugLog.h>

size_t HashFile(const std::filesystem::path& path) {
    constexpr size_t BUFFER_SIZE = 1024 * 64;
    std::vector<char> buffer(BUFFER_SIZE);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Debug::LogError("File {} doesnt exist", path.string());
        return 0;
    }

    XXH3_state_t* state = XXH3_createState();
    XXH3_64bits_reset(state);

    while (file) {
        file.read(buffer.data(), buffer.size());
        const std::streamsize readBytes = file.gcount();
        if (readBytes > 0) {
            XXH3_64bits_update(state, buffer.data(), readBytes);
        }
    }

    const uint64_t hash = XXH3_64bits_digest(state);
    XXH3_freeState(state);

    return hash;
}

size_t HashString(const std::string& str) {
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : str) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}
