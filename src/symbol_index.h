#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct CodeSymbol {
    std::string name;
    std::filesystem::path file;
    int line = 0;
    std::string kind;
    std::string scope;
    std::string inherits;
};

bool parseCtagsOutput(std::string_view output, const std::filesystem::path &root,
                      std::vector<CodeSymbol> &symbols, std::string &error);
bool buildCtagsIndex(const std::vector<std::filesystem::path> &files,
                     const std::filesystem::path &root,
                     std::vector<CodeSymbol> &symbols, std::string &error);
