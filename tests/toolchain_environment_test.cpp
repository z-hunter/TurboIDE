#include "../src/toolchain_environment.h"

#include <cwchar>

int main() {
    const auto environment = toolchainEnvironment(L"C:\\toolchain\\bin\\gcc.exe");
    const wchar_t *entry = environment.data();
    while (*entry) {
        if (_wcsnicmp(entry, L"PATH=", 5) == 0)
            return std::wcsncmp(entry + 5, L"C:\\toolchain\\bin;", 17) == 0 ? 0 : 1;
        entry += std::wcslen(entry) + 1;
    }
    return 1;
}
