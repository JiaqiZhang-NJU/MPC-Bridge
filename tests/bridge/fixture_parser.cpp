// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
int wmain() {
    wchar_t path[32768];
    GetModuleFileNameW(nullptr, path, 32768);
    std::ifstream metadata(std::filesystem::path(path).replace_extension(L".json"), std::ios::binary);
    if (!metadata) return 1;
    std::cout << metadata.rdbuf();
    return 0;
}
