#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: v4l2loopback-helper add|remove [args]\n";
        return 1;
    }

    std::string cmd;

    if (std::string(argv[1]) == "add") {
        if (argc < 3) {
            std::cerr << "Missing camera name\n";
            return 1;
        }

        cmd = "v4l2loopback-ctl add --exclusive-caps 1 --name=\"" +
              std::string(argv[2]) + "\"";
    }
    else if (std::string(argv[1]) == "remove") {
        if (argc < 3) {
            std::cerr << "Missing video number\n";
            return 1;
        }

        cmd = "v4l2loopback-ctl delete " + std::string(argv[2]);
    }
    else {
        std::cerr << "Unknown command\n";
        return 1;
    }

    return std::system(cmd.c_str());
}
