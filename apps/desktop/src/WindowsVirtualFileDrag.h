#pragma once
#include <FileShareModule.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class WindowsVirtualFileDrag {
  public:
    enum class Result { Completed, Cancelled, Failed };
    using ProgressFn = std::function<void(const std::string &, uint64_t, uint64_t, uint64_t, uint64_t)>;
    static Result Start(std::shared_ptr<FileShareModule> module, std::vector<FileEntry> entries, ProgressFn progress);
};
