#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>

int wmain(int argc, wchar_t **argv) {
    std::filesystem::path output;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::wstring_view(argv[i]) == L"-o")
            output = argv[++i];

    std::cout << "fake.c:1:1: warning: first message\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::cout << "fake.c:2:1: error: second message\n" << std::flush;
    if (!output.empty())
        std::ofstream(output).put('\0');
    return 0;
}
