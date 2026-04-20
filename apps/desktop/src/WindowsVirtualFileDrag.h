#pragma once

#include <filesystem>
#include <functional>
#include <vector>

class WindowsVirtualFileDrag
{
public:
    using ResolvePathsFn = std::function<std::vector<std::filesystem::path>()>;

    static bool Start(ResolvePathsFn resolver);
};
