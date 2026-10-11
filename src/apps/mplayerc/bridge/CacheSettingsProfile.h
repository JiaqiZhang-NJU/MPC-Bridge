// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MediaCache.h"
namespace Bridge {
inline CacheSettings ReadCacheSettings() {
    auto& profile = AfxGetProfile();
    CacheSettings settings;
    profile.ReadBool(L"MPC-Bridge Cache", L"Enabled", settings.enabled);
    profile.ReadInt(L"MPC-Bridge Cache", L"ForwardSeconds", settings.forwardSeconds, 1, 120);
    profile.ReadInt(L"MPC-Bridge Cache", L"BackSeconds", settings.backSeconds, 0, 120);
    int capacity = 256;
    profile.ReadInt(L"MPC-Bridge Cache", L"DiskMiB", capacity, 16, 256);
    settings.diskBytes = capacity * 1024ull * 1024;
    CStringW directory;
    profile.ReadString(L"MPC-Bridge Cache", L"Directory", directory);
    settings.directory = directory.GetString();
    return settings;
}
}
