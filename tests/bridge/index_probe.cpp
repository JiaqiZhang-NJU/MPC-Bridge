// SPDX-License-Identifier: GPL-3.0-or-later
#include "apps/mplayerc/bridge/MediaCache.h"
#include <filesystem>
#include <fstream>
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    std::filesystem::path path(argv[1]);
    std::ifstream file(path, std::ios::binary);
    const auto length = std::filesystem::file_size(path);
    auto tracks = Bridge::BuildMediaTimeIndex(length, [&](std::uint64_t offset, std::size_t count, std::vector<std::uint8_t>& bytes) {
        bytes.resize(count); file.clear(); file.seekg(offset);
        return static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), count));
    });
    std::cout << tracks.size() << '\n';
    for (const auto& track : tracks) {
        std::cout << track.samples.size() << ' ' << track.samples.front().bytes.first << ' ' << track.samples.back().end << '\n';
    }
}
