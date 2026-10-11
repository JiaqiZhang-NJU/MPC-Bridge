// SPDX-License-Identifier: GPL-3.0-or-later
#include "apps/mplayerc/bridge/MediaCache.h"
#include <iostream>
#include <chrono>
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    Bridge::CacheSettings settings; settings.directory = argv[2];
    if (argc > 3) settings.diskBytes = std::stoull(argv[3]);
    Bridge::MediaCache cache(settings);
    const auto route = cache.Register(argv[1], L"MPC-Bridge-test", L"https://www.bilibili.com/");
    std::wcout << L"ROUTE " << route << std::endl;
    cache.Select({route});
    std::string command;
    while (std::getline(std::cin, command)) {
        if (command == "quit") break;
        if (command == "idle") { cache.Select({}); std::cout << "IDLE" << std::endl; }
        if (command == "clear") cache.Clear();
        if (command.substr(0, 7) == "update ") cache.Update(std::stod(command.substr(7)), 1.0, true);
        if (command == "status") {
            const auto status = cache.Status();
            std::cout << "STATUS " << status.bytes << ' ' << status.requests << ' ' << status.hits << ' ' << status.forwardSeconds << std::endl;
        }
    }
    const auto begin = std::chrono::steady_clock::now();
    cache.Cancel();
    std::cout << "CANCEL " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count() << std::endl;
}
