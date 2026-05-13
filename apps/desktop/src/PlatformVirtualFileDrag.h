#pragma once

#include <QObject>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class PlatformVirtualFileDrag
{
public:
    using ResolvePathsFn = std::function<std::vector<std::filesystem::path>()>;
    using ResolvePathFn = std::function<std::filesystem::path()>;
    using PromiseCompletionFn = std::function<void(bool, std::vector<std::filesystem::path>)>;

    struct PromisedFile {
        std::string fileName;
        ResolvePathFn resolver;
    };

    static bool Start(QObject* dragSource, ResolvePathsFn resolver);
    static bool StartPromisedFiles(QObject* dragSource, std::vector<PromisedFile> files, PromiseCompletionFn completion);
};
