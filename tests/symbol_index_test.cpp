#include "../src/symbol_index.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main() {
    const auto root = std::filesystem::path(L"C:/work/project");
    const std::string output =
        "!_TAG_FILE_FORMAT\t2\t/original ctags format/\n"
        "run\tsrc/main.cpp\t12;\"\tf\tline:12\tscope:class:app::Runner\tinherits:Base\n"
        "run\tsrc/na\xC3\xAFve.cpp\t27;\"\tf\tline:27\n";
    std::vector<CodeSymbol> symbols;
    std::string error;
    assert(parseCtagsOutput(output, root, symbols, error));
    assert(error.empty());
    assert(symbols.size() == 2);
    assert(symbols[0].name == "run");
    assert(symbols[0].file == root / L"src/main.cpp");
    assert(symbols[0].line == 12);
    assert(symbols[0].kind == "f");
    assert(symbols[0].scope == "class:app::Runner");
    assert(symbols[0].inherits == "Base");
    assert(symbols[1].file.filename().u8string() == "na\xC3\xAFve.cpp");
    assert(symbols[1].line == 27);

    const auto previous = symbols;
    assert(!parseCtagsOutput("broken record\n", root, symbols, error));
    assert(!error.empty());
    assert(symbols.size() == previous.size());

    wchar_t ctagsPath[32768]{};
    const DWORD ctagsLength = GetEnvironmentVariableW(L"TURBOIDE_CTAGS", ctagsPath, 32768);
    wchar_t executable[32768]{};
    const DWORD executableLength = GetModuleFileNameW(nullptr, executable, 32768);
    const bool bundled = executableLength && executableLength < 32768 &&
        std::filesystem::is_regular_file(std::filesystem::path(executable).parent_path() /
                                         L"tools" / L"ctags" / L"ctags.exe");
    if ((ctagsLength && ctagsLength < 32768) || bundled) {
        const auto integrationRoot = std::filesystem::temp_directory_path() /
            (L"TurboIDE-symbol-index-test-" + std::to_wstring(GetCurrentProcessId()));
        assert(std::filesystem::create_directory(integrationRoot));
        const auto source = integrationRoot / L"fixture.cpp";
        {
            std::ofstream file(source, std::ios::binary | std::ios::trunc);
            file << "namespace sample { struct Widget {}; int calculate() { return 1; } }\n";
        }
        wchar_t expectFailure[8]{};
        const bool rejectsTool = GetEnvironmentVariableW(
            L"TURBOIDE_CTAGS_EXPECT_FAILURE", expectFailure, 8) != 0;
        const bool indexed = buildCtagsIndex({source}, integrationRoot, symbols, error);
        if (rejectsTool) {
            assert(!indexed);
            assert(error.find("not a compatible Universal Ctags") != std::string::npos);
        } else {
            assert(indexed);
            assert(std::any_of(symbols.begin(), symbols.end(), [](const CodeSymbol &symbol) {
                return symbol.name == "calculate" && symbol.line == 1;
            }));
        }
        std::filesystem::remove_all(integrationRoot);
    }
    return 0;
}
