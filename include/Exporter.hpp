#pragma once

#include <string>

class Exporter {
public:
    int run(int argc, char* argv[]);

private:
    struct Options {
        std::string input;
        std::string outDir;
        bool printStats = false;
        bool validate   = false;
    };

    Options parseArgs(int argc, char* argv[]);
    bool exportAssemblyAndComponents(const Options& opt);
};
