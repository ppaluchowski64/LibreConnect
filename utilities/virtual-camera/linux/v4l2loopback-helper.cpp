#include <algorithm>
#include <cerrno>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace {
int RunCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        return 1;
    }

    std::vector<char*> execArgs;
    execArgs.reserve(args.size() + 1);
    for (const std::string& arg : args) {
        execArgs.push_back(const_cast<char*>(arg.c_str()));
    }
    execArgs.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        return 1;
    }

    if (pid == 0) {
        execvp(execArgs[0], execArgs.data());
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno == EINTR) {
            continue;
        }
        return 1;
    }

    if (!WIFEXITED(status)) {
        return 1;
    }

    return WEXITSTATUS(status);
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: v4l2loopback-helper add|remove [args]\n";
        return 1;
    }

    std::vector<std::string> args;

    if (std::string(argv[1]) == "add") {
        if (argc < 3) {
            std::cerr << "Missing camera name\n";
            return 1;
        }

        args = {
            "v4l2loopback-ctl",
            "add",
            "--exclusive-caps=1",
            "--name",
            std::string(argv[2])
        };
    }
    else if (std::string(argv[1]) == "remove") {
        if (argc < 3) {
            std::cerr << "Missing video number\n";
            return 1;
        }

        const std::string videoNumber = argv[2];
        if (videoNumber.empty() || !std::all_of(videoNumber.begin(), videoNumber.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            std::cerr << "Invalid video number\n";
            return 1;
        }

        args = {
            "v4l2loopback-ctl",
            "delete",
            videoNumber
        };
    }
    else {
        std::cerr << "Unknown command\n";
        return 1;
    }

    return RunCommand(args);
}
