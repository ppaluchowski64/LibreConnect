#pragma once

#include <QObject>

#include <filesystem>
#include <functional>
#include <vector>

class PlatformVirtualFileDrag
{
public:
    using ResolvePathsFn = std::function<std::vector<std::filesystem::path>()>;

    static bool Start(QObject* dragSource, ResolvePathsFn resolver);
};

